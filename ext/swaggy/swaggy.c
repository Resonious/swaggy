#include "ruby.h"
#include "libfyaml.h"
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#define assert RUBY_ASSERT

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
    VALUE openapi_path;
    mmapped_file_t openapi_file;
    struct fy_document *yaml_document;
} swaggy_rack_t;

/*
 * ==============================
 * Custom allocation / GC support
 * ==============================
 */

static void swaggy_rack_mark(void *data) {
    swaggy_rack_t *trace = (swaggy_rack_t*)data;
    rb_gc_mark(trace->openapi_path);
}

static void swaggy_rack_free(void *data) {
    swaggy_rack_t *swaggy_rack = (swaggy_rack_t*)data;

    if (swaggy_rack->yaml_document) {
        fy_document_destroy(swaggy_rack->yaml_document);
    }
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

    swaggy_rack->openapi_path = Qnil;
    swaggy_rack->yaml_document = NULL;

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

    swaggy_rack->yaml_document = fy_document_build_from_string(
        NULL,
        swaggy_rack->openapi_file.data,
        swaggy_rack->openapi_file.len
    );

    return Qnil;
}

static VALUE swaggy_rack_call(VALUE self, VALUE env) {
    swaggy_rack_t *swaggy_rack = RTYPEDDATA_DATA(self);

    // Path from request
    VALUE path = rb_hash_aref(env, rb_str_new_static("PATH_INFO", 9));
    const char *path_ptr = RSTRING_PTR(path);
    size_t path_len = RSTRING_LEN(path);

    // Method from request
    VALUE method = rb_hash_aref(env, rb_str_new_static("REQUEST_METHOD", 14));
    const char *method_ptr = RSTRING_PTR(method);
    size_t method_len = RSTRING_LEN(method);

    // Debug printing
    for (size_t i = 0; i < method_len; i++) { putc(method_ptr[i], stdout); }
    putc(' ', stdout);
    for (size_t i = 0; i < path_len; i++) { putc(path_ptr[i], stdout); }
    putc('\n', stdout);

    // Ruby hash to hold path parameters
    VALUE path_params = rb_hash_new();

    // OK let's dig into the openapi doc
    struct fy_node *paths = fy_node_by_path(
        fy_document_root(swaggy_rack->yaml_document),
        "/paths", 6,
        FYNWF_FOLLOW
    );
    if (fy_node_get_type(paths) != FYNT_MAPPING) {
        rb_raise(rb_eRuntimeError, "Expected paths to be an object");
    }

    void *paths_iter = NULL;
    struct fy_node_pair *path_node_pair;
    struct fy_node *path_node;

    path_node_pair = fy_node_mapping_iterate(paths, &paths_iter);

    // Looping through all the openapi doc paths
    while (path_node_pair != NULL) {
        path_node = fy_node_pair_key(path_node_pair);

        const char *key_ptr;
        size_t key_len;
        key_ptr = fy_node_get_scalar(path_node, &key_len);
        if (key_ptr == NULL) { rb_raise(rb_eRuntimeError, "OpenAPI path was not a string"); }

        // We'll use this to keep track of whether we've seen a path parameters.
        // This is to facilitate blowing up the path_params hash each iteration,
        // which might just be bad in the first place but oh well.
        bool path_param_added = false;

        const char *api_path_ptr = key_ptr;
        size_t api_path_len = key_len;

        // Now we loop through each character in the API path and the request path.
        size_t api_path_i = 0;
        size_t req_path_i = 0;
        while (api_path_i < api_path_len && req_path_i < path_len) {
            if (req_path_i > 0 && path_ptr[req_path_i] == '/' && path_ptr[req_path_i - 1] == '/') {
                req_path_i++;
            } else if (api_path_ptr[api_path_i] == '{') {
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
                goto not_found;
            }
        }

        // Accept trailing slashes
        while (path_ptr[req_path_i] == '/') {
            req_path_i++;
        }

        // If both paths ended at the same time, we've found a match
        if (api_path_i == api_path_len && req_path_i == path_len) {
            goto found;
        }

        // Otherwise, continue on to the next API path
        if (path_param_added) {
            rb_hash_clear(path_params);
            path_param_added = false;
        }
        path_node_pair = fy_node_mapping_iterate(paths, &paths_iter);
    }
    goto not_found;

found:
    {
        // Method "query" in the form of e.g. "/get" or "/post".
        // This is for querying with libfyaml's fy_node_by_path.
        char method_query_ptr[12];
        memset(method_query_ptr, 0, sizeof(method_query_ptr));
        method_query_ptr[0] = '/';
        assert(method_len < sizeof(method_query_ptr) - 1);
        memcpy(method_query_ptr + 1, method_ptr, method_len);
        for (size_t i = 0; i < method_len + 1; ++i) {
            method_query_ptr[i + 1] = rb_tolower(method_query_ptr[i + 1]);
        }

        struct fy_node *methods_node = fy_node_pair_value(path_node_pair);
        struct fy_node *method_spec = fy_node_by_path(
            methods_node,
            method_query_ptr, method_len + 1,
            FYNWF_FOLLOW
        );
        if (fy_node_get_type(method_spec) != FYNT_MAPPING) {
            goto not_found;
        }

        struct fy_node *summary_node = fy_node_by_path(
            method_spec,
            "/summary", 8,
            FYNWF_FOLLOW
        );
        size_t summary_len;
        const char *summary = fy_node_get_scalar(summary_node, &summary_len);

        // return [200, {}, [summary]]
        VALUE status = INT2NUM(200);
        VALUE headers = rb_hash_new();
        VALUE body = rb_ary_new();
        VALUE result = rb_ary_new();

        rb_ary_push(body, rb_str_new_static(summary, summary_len));

        rb_ary_push(result, status);
        rb_ary_push(result, headers);
        rb_ary_push(result, body);

        return result;
    }

not_found:
    {
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
}

void Init_swaggy() {
    VALUE mSwaggy = rb_define_module("Swaggy");
    VALUE cSwaggyRack = rb_define_class_under(mSwaggy, "Rack", rb_cObject);
    rb_define_alloc_func(cSwaggyRack, swaggy_rack_allocate);

    rb_define_method(cSwaggyRack, "initialize", swaggy_rack_init, 1);
    rb_define_method(cSwaggyRack, "call", swaggy_rack_call, 1);
    rb_define_singleton_method(cSwaggyRack, "hello", swaggy_rack_hello, 0);
}

