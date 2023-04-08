#include "ruby.h"

static VALUE swaggy_rack_hello(VALUE self) {
    return rb_str_new2("Hello from the C extension!");
}

void Init_swaggy() {
    VALUE mSwaggy = rb_define_module("Swaggy");
    VALUE mSwaggyRack = rb_define_module_under(mSwaggy, "Rack");

    rb_define_singleton_method(mSwaggyRack, "hello", swaggy_rack_hello, 0);
}

