// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that base class initialization works correctly when the base contains
// member pointers (non-zero null constant: member pointers are -1, not 0).
// Previously this hit an errorNYI fallthrough, leaving the base uninitialized.

struct Base {
    int Base::*mp;
};

struct Derived : Base {
    int x;
    Derived() : x(42) {}
};

void test() {
    Derived d;
}

// CHECK: define {{.*}}void @_Z4testv()
// CHECK:   call void @_ZN7DerivedC1Ev(
