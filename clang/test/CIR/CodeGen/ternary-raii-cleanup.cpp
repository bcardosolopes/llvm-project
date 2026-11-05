// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify RAII temporaries in ternary and short-circuit expressions
// compile and have proper destructor calls.

struct W {
    int val;
    W(int v) : val(v) {}
    ~W() {}
    operator int() const { return val; }
    operator bool() const { return val != 0; }
};

// CHECK: define {{.*}} i32 @_Z12test_ternaryv()
// CHECK:   call void @_ZN1WC1Ei(
// CHECK:   call void @_ZN1WD1Ev(
int test_ternary() {
    return W(1) ? W(10).val : W(20).val;
}

// CHECK: define {{.*}} @_Z13test_and_raiiv()
// CHECK:   call void @_ZN1WC1Ei(
bool test_and_raii() {
    return W(1) && W(2);
}

// CHECK: define {{.*}} @_Z12test_or_raiiv()
// CHECK:   call void @_ZN1WC1Ei(
bool test_or_raii() {
    return W(0) || W(1);
}
