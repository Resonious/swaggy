#include "ruby.h"
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

void Init_swaggy() {
    VALUE mSwaggy = rb_define_module("Swaggy");
    VALUE cSwaggyRack = rb_define_class_under(mSwaggy, "Rack", rb_cObject);
    rb_define_alloc_func(cSwaggyRack, swaggy_rack_allocate);

    rb_define_method(cSwaggyRack, "initialize", swaggy_rack_init, 1);
    rb_define_singleton_method(cSwaggyRack, "hello", swaggy_rack_hello, 0);
}

