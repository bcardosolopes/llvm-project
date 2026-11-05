// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu -std=c++20 %s -o /dev/null
// __builtin_get_vtable_pointer — previously XFAIL, now implemented.

struct B { virtual void f() {} };
const void *get_vt(B *b) { return __builtin_get_vtable_pointer(b); }
