// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that explicit base initializer with polymorphic base class compiles
// correctly. Previously, the base subobject CIR type differed from the
// complete object type for polymorphic classes, causing a cir.store type
// mismatch during null base class initialization.

struct A {
    int x;
    virtual ~A() {}
};

// CHECK-LABEL: @_ZN1BC2Ev
// CHECK: cir.store
struct B : A {
    B() : A() {}
};

struct Base {
    int x;
    virtual int get() { return x; }
};

// CHECK-LABEL: @_ZN7DerivedC2Ev
// CHECK: cir.store
struct Derived : Base {
    Derived() : Base() { x = 42; }
};

void test() {
    B b;
    Derived d;
}
