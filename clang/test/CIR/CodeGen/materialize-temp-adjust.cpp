// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll

// Test MaterializeTemporaryExpr with subobject adjustments.
// Previously hit errorNYI for derived-to-base and field adjustments.

struct Base {
    int x;
};

struct Derived : Base {
    int y;
};

Derived make_derived();

// Binding a reference to a base subobject of a temporary triggers
// a DerivedToBaseAdjustment.
// CHECK-LABEL: cir.func{{.*}} @_Z18test_base_ref_bindv
// CHECK: cir.base_class_addr
int test_base_ref_bind() {
    const Base& b = make_derived();
    return b.x;
}

struct Outer {
    int a;
    int b;
};

Outer make_outer();

// Binding a reference to a field of a temporary triggers a FieldAdjustment.
// CHECK-LABEL: cir.func{{.*}} @_Z19test_field_ref_bindv
// CHECK: cir.get_member
int test_field_ref_bind() {
    const int& r = make_outer().b;
    return r;
}
