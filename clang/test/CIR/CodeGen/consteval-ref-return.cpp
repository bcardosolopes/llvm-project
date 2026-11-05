// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that consteval functions returning references are evaluated at compile
// time and produce correct code without trying to emit the consteval function.

constexpr int i_const = 5;

consteval const int &retI() {
    return i_const;
}

const int &test_retRefI() {
    return retI();
}

// CHECK: @_ZL7i_const = internal constant i32 5
// CHECK-LABEL: define {{.*}} @_Z12test_retRefIv
// CHECK: ret ptr @_ZL7i_const
