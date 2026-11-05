// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fcxx-exceptions -fexceptions -mconstructor-aliases -O1 \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll
//
// Test that throwing a derived exception from inside a catch handler compiles
// correctly when constructor aliases are enabled. Previously, destructor
// aliasing (Derived::~Derived -> Base::~Base) caused a cir.get_global type
// mismatch because the result type used the derived class but the global
// referenced the base class destructor.

struct Base {
    int code;
    Base(int c) : code(c) {}
    virtual ~Base() = default;
};

struct Derived : Base {
    Derived(int c) : Base(c) {}
};

int test() {
    try {
        try {
            throw Derived(1);
        } catch (const Base&) {
            throw Derived(2);  // Previously crashed: get_global type mismatch
        }
    } catch (const Base& e) {
        return e.code;
    }
    return 0;
}

// CHECK-LABEL: define {{.*}} @_Z4testv(
// CHECK: invoke void @__cxa_throw(
// CHECK: invoke void @__cxa_throw(
// CHECK: }
