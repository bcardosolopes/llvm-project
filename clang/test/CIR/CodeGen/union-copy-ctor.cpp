// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that implicit copy/move constructors of union types copy data.
// Previously, CIR generated an empty body (no memcpy) for union
// move-constructors, causing the constructed union to have zeroed data.
// This broke std::swap and std::function::operator= which rely on
// move-constructing a temporary to save original data.

union U {
    char c;
    int i;
};

// The implicit copy constructor should emit a memcpy.
// CHECK-LABEL: define {{.*}}@_Z9test_copyv()
// CHECK: llvm.memcpy
void test_copy() {
    U a;
    a.i = 42;
    U b(a);
}

// The implicit move constructor should also emit a memcpy.
// CHECK-LABEL: define {{.*}}@_Z9test_movev()
// CHECK: llvm.memcpy
void test_move() {
    U a;
    a.i = 42;
    U b(static_cast<U&&>(a));
}
