#include "ruby.h"

static VALUE swaggy_hello(VALUE self) {
    return rb_str_new2("Hello from the C extension!");
}

void Init_swaggy() {
    VALUE module = rb_define_module("Swaggy");
    rb_define_singleton_method(module, "hello", swaggy_hello, 0);
}

