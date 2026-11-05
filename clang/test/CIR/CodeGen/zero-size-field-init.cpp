// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test: [[no_unique_address]] empty members with non-trivial
// constructors must have their constructor side effects emitted, even though
// zero-size fields have no storage in the CIR record type.

int side_effect_counter;

struct Empty {
    Empty() { ++side_effect_counter; }
};

struct Container {
    [[no_unique_address]] Empty e;
    int value;
    Container(int v) : e(), value(v) {}
};

// CHECK-LABEL: define {{.*}}@_ZN9ContainerC2Ei(
// The constructor must call Empty's constructor for its side effects.
// CHECK: call void @_ZN5EmptyC1Ev(
// CHECK: ret void

void test_ctor_side_effects() {
    Container c(42);
}
