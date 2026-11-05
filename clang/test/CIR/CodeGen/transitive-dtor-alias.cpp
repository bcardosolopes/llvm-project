// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Regression test: transitive replacement chains
// must be followed. When D1->D2 and D2->base_D2, D1 must resolve to base_D2.
// This tests destructor aliasing with inheritance chains.

struct Base {
    virtual ~Base() = default;
};

struct Derived : Base {
    ~Derived() override = default;
};

void test() {
    Derived d;
}

// Both the base and derived destructors should exist and be resolvable.
// CHECK: define {{.*}}@_Z4testv()
// CHECK: call void @_ZN7DerivedD1Ev
