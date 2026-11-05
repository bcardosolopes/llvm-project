// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics -fclangir -emit-llvm -o - %s | FileCheck %s

#define __ptrauth(...) __ptrauth(__VA_ARGS__)

extern __UINTPTR_TYPE__ test_int;

__UINTPTR_TYPE__ __ptrauth(3, 1, 23) g3 = (__UINTPTR_TYPE__)&test_int;
// CHECK: @g3 = global i64 ptrtoint (ptr ptrauth (ptr @test_int, i32 3, i64 23, ptr @g3) to i64), align 8

__INTPTR_TYPE__ __ptrauth(1, 1, 712) ga[3] = {0, 0, (__UINTPTR_TYPE__)&test_int};
// CHECK: @ga = global [3 x i64] [i64 0, i64 0, i64 ptrtoint (ptr ptrauth (ptr @test_int, i32 1, i64 712, ptr getelementptr inbounds ([3 x i64], ptr @ga, i32 0, i32 2)) to i64)], align 8

struct B {
  __INTPTR_TYPE__ __ptrauth(1, 1, 1276) f0;
  __INTPTR_TYPE__ __ptrauth(1, 1, 23674) f1;
  __INTPTR_TYPE__ __ptrauth(1, 1, 163) f2;
};

struct B gs2 = {0, 0, (__UINTPTR_TYPE__)&test_int};
// CHECK: @gs2 = global %struct.B { i64 0, i64 0, i64 ptrtoint (ptr ptrauth (ptr @test_int, i32 1, i64 163, ptr getelementptr inbounds (%struct.B, ptr @gs2, i32 0, i32 2)) to i64) }, align 8
