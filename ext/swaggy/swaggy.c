#include "ruby.h"
#include "json.h"
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

typedef struct mmapped_file_st {
    /* file handle, should be r+ */
    FILE *handle;
    /* mmapped data ptr */
    char *data;
    /* length of file and data */
    size_t len;
} mmapped_file_t;
static void file_unmap_memory(mmapped_file_t *file);

typedef struct swaggy_rack_st {
    // Holding onto static strings
    VALUE sPATH_INFO;

    VALUE openapi_path;
    mmapped_file_t openapi_file;
} swaggy_rack_t;

/*
 * ==============================
 * Custom allocation / GC support
 * ==============================
 */

static void swaggy_rack_mark(void *data) {
    swaggy_rack_t *trace = (swaggy_rack_t*)data;
    rb_gc_mark(trace->sPATH_INFO);
    rb_gc_mark(trace->openapi_path);
}

static void swaggy_rack_free(void *data) {
    swaggy_rack_t *swaggy_rack = (swaggy_rack_t*)data;

    if (swaggy_rack->openapi_file.data) {
        file_unmap_memory(&swaggy_rack->openapi_file);
    }
    if (swaggy_rack->openapi_file.handle) {
        fclose(swaggy_rack->openapi_file.handle);
    }

    xfree(swaggy_rack);
}

size_t swaggy_rack_size(const void* _data) {
    return sizeof(swaggy_rack_t);
}

static const rb_data_type_t swaggy_rack_type =
{
    .wrap_struct_name = "Trace",
    .function =
    {
        .dmark = swaggy_rack_mark,
        .dfree = swaggy_rack_free,
        .dsize = swaggy_rack_size,
    },
    .data = NULL,
    .flags = RUBY_TYPED_FREE_IMMEDIATELY
};

static VALUE swaggy_rack_allocate(VALUE klass) {
    swaggy_rack_t *swaggy_rack;
    VALUE result = TypedData_Make_Struct(klass, swaggy_rack_t, &swaggy_rack_type, swaggy_rack);

    swaggy_rack->sPATH_INFO = rb_str_new2("PATH_INFO");
    swaggy_rack->openapi_path = Qnil;

    return result;
}

/*
 * ====================
 * File/mmap management
 * ====================
 */

static void file_map_memory(mmapped_file_t *file) {
    assert(file);
    if (file->handle == NULL) {
        rb_raise(rb_eRuntimeError, "File not open: %s", strerror(errno));
    }

    file->len = lseek(fileno(file->handle), 0, SEEK_END);

    file->data = mmap(
        NULL,
        file->len,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fileno(file->handle),
        0
    );

    if (file->data == MAP_FAILED) {
        rb_raise(rb_eRuntimeError, "Failed to mmap: %s", strerror(errno));
    }
}

static void file_unmap_memory(mmapped_file_t *file) {
    assert(file);
    assert(file->data);

    int result = munmap(file->data, file->len);

    if (result == -1) {
        rb_raise(rb_eRuntimeError, "Failed to munmap: %s", strerror(errno));
    }

    file->data = NULL;
}

/*
 * ====================
 * Swaggy::Rack methods
 * ====================
 */

static VALUE swaggy_rack_hello(VALUE self) {
    return rb_str_new2("Hello from the C extension!");
}

static VALUE swaggy_rack_init(VALUE self, VALUE openapi_path) {
    swaggy_rack_t *swaggy_rack = RTYPEDDATA_DATA(self);
    swaggy_rack->openapi_path = openapi_path;

    // Open the file
    const char *openapi_path_str = StringValueCStr(openapi_path);
    swaggy_rack->openapi_file.handle = fopen(openapi_path_str, "r+");
    if (swaggy_rack->openapi_file.handle == NULL) {
        rb_raise(rb_eRuntimeError, "Failed to open file: %s", strerror(errno));
    }
    file_map_memory(&swaggy_rack->openapi_file);

    return Qnil;
}

static VALUE swaggy_rack_call(VALUE self, VALUE env) {
    swaggy_rack_t *swaggy_rack = RTYPEDDATA_DATA(self);

    // Path from request
    VALUE path = rb_hash_aref(env, swaggy_rack->sPATH_INFO);
    const char *path_ptr = RSTRING_PTR(path);
    size_t path_len = RSTRING_LEN(path);

    for (size_t i = 0; i < path_len; i++) {
        putc(path_ptr[i], stdout);
    }
    putc('\n', stdout);

    // Method from request
    VALUE method = rb_hash_aref(env, rb_str_new2("REQUEST_METHOD"));
    const char *method_ptr = RSTRING_PTR(method);
    size_t method_len = RSTRING_LEN(method);

    // Ruby hash to hold path parameters
    VALUE path_params = rb_hash_new();

    // OK let's dig into the openapi doc
    const char *openapi_doc = swaggy_rack->openapi_file.data;
    struct json paths = json_get(openapi_doc, "paths");

    // If while looping through paths we find the path we're looking for, we'll
    // populate this bad boy.
    struct json found_openapi_path_spec = json_parse("null");

    // Looping through all the openapi doc paths
    for (struct json key = json_first(paths); json_exists(key); key = json_next(key)) {
        found_openapi_path_spec = json_next(key);

        const char *key_ptr = json_raw(key);
        size_t key_len = json_raw_length(key);

        // We'll use this to keep track of whether we've seen a path parameters.
        // This is to facilitate blowing up the path_params hash each iteration,
        // which might just be bad in the first place but oh well.
        bool path_param_added = false;

        // Skip the starting "
        const char *api_path_ptr = key_ptr + 1;
        // Ignore the trailing "
        size_t api_path_len = key_len - 2;

        size_t api_path_i = 0;
        size_t req_path_i = 0;
        while (api_path_i < api_path_len && req_path_i < path_len) {
            if (api_path_ptr[api_path_i] == '{') {
                // We've found a path parameter
                path_param_added = true;
                size_t param_start = api_path_i + 1;
                size_t param_end = param_start;
                while (api_path_ptr[param_end] != '}') {
                    param_end++;
                }

                // Add the path parameter to the hash
                // NOTE: this is a bit wasteful. If there are many paths with similar path
                // parameters, we'll be adding those to the hash and then blowing it up each
                // time.
                size_t param_len = param_end - param_start;
                const char *param_name = api_path_ptr + param_start;

                size_t param_value_end = req_path_i;
                while (param_value_end < path_len && path_ptr[param_value_end] != '/') {
                    param_value_end++;
                }
                size_t param_value_len = param_value_end - req_path_i;
                const char *param_value = path_ptr + req_path_i;

                rb_hash_aset(
                    path_params,
                    rb_str_new_frozen(rb_str_new_static(param_name, param_len)),
                    rb_str_new(param_value, param_value_len)
                );

                api_path_i = param_end + 1;
                req_path_i = param_value_end;
            } else if (api_path_ptr[api_path_i] == path_ptr[req_path_i]) {
                api_path_i++;
                req_path_i++;
            } else {
                found_openapi_path_spec = json_parse("null");
                goto done;
            }
        }
        if (api_path_i == api_path_len && req_path_i == path_len) {
            goto done;
        }

        if (path_param_added) rb_hash_clear(path_params);
        key = found_openapi_path_spec;
    }
done:
    if (json_type(found_openapi_path_spec) == JSON_NULL) {
        VALUE status = INT2NUM(404);
        VALUE headers = rb_hash_new();
        VALUE body = rb_ary_new();
        VALUE result = rb_ary_new();

        rb_ary_push(body, rb_str_new2("Path not found man"));

        rb_ary_push(result, status);
        rb_ary_push(result, headers);
        rb_ary_push(result, body);

        return result;
    }

    // return [200, {}, ["Hello World"]]
    VALUE status = INT2NUM(200);
    VALUE headers = rb_hash_new();
    VALUE body = rb_ary_new();
    VALUE result = rb_ary_new();

    rb_ary_push(body, rb_str_new2("Hello World"));

    rb_ary_push(result, status);
    rb_ary_push(result, headers);
    rb_ary_push(result, body);

    return result;
}

void Init_swaggy() {
    VALUE mSwaggy = rb_define_module("Swaggy");
    VALUE cSwaggyRack = rb_define_class_under(mSwaggy, "Rack", rb_cObject);
    rb_define_alloc_func(cSwaggyRack, swaggy_rack_allocate);

    rb_define_method(cSwaggyRack, "initialize", swaggy_rack_init, 1);
    rb_define_method(cSwaggyRack, "call", swaggy_rack_call, 1);
    rb_define_singleton_method(cSwaggyRack, "hello", swaggy_rack_hello, 0);
}

