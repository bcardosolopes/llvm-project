// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Regression test: When ConstantEmitter fails
// for a ConstantExpr, visit the subexpression instead of returning undef.
// Before the fix, this would return undef instead of calling square().

constexpr int square(int n) { return n * n; }

int test() {
    return square(5);
}

// The call should produce a real function call, not undef.
// CHECK-LABEL: define {{.*}}@_Z4testv()
// CHECK: call {{.*}}@_Z6squarei(i32 noundef 5)
// CHECK-NOT: undef
// CHECK: ret i32
