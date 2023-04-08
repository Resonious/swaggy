#include "ruby.h"

typedef struct swaggy_rack_st {
    VALUE openapi_path;
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
    swaggy_rack_t *trace = (swaggy_rack_t*)data;
    xfree(trace);
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
 * Swaggy::Rack methods
 * ====================
 */

static VALUE swaggy_rack_hello(VALUE self) {
    return rb_str_new2("Hello from the C extension!");
}

static VALUE swaggy_rack_init(VALUE self, VALUE openapi_path) {
    return Qnil;
}

void Init_swaggy() {
    VALUE mSwaggy = rb_define_module("Swaggy");
    VALUE mSwaggyRack = rb_define_class_under(mSwaggy, "Rack", rb_cObject);

    rb_define_method(mSwaggyRack, "initialize", swaggy_rack_init, 1);
    rb_define_singleton_method(mSwaggyRack, "hello", swaggy_rack_hello, 0);
}

