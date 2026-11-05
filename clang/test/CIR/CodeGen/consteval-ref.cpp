// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Verify that consteval functions returning references are evaluated at
// compile time and produce direct address references, not function calls.

constexpr int i_const = 5;

consteval const int &retI() {
    return i_const;
}

// Test 1: consteval reference return used as lvalue (return by ref)
const int &test_retRefI() {
    return retI();
}

// CIR: cir.func {{.*}} @_Z12test_retRefIv
// CIR:   cir.const #cir.global_view<@_ZL7i_const>
// CIR-NOT: cir.call @_Z4retIv

// LLVM: define {{.*}} @_Z12test_retRefIv
// LLVM:   ret ptr @_ZL7i_const
// LLVM-NOT: call {{.*}} @_Z4retIv

// Test 2: consteval reference return used as scalar (load through ref)
int test_retI() {
    return retI();
}

// CIR: cir.func {{.*}} @_Z9test_retIv
// CIR:   cir.const #cir.global_view<@_ZL7i_const>
// CIR-NOT: cir.call @_Z4retIv

// LLVM: define {{.*}} @_Z9test_retIv
// LLVM:   %[[V:.*]] = load i32, ptr @_ZL7i_const
// LLVM:   ret i32 %[[V]]
// LLVM-NOT: call {{.*}} @_Z4retIv
