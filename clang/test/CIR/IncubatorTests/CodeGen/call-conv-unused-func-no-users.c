// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

int helper(int x) { return x + 42; }

int untouched(int x) { return x * 3; }

// CHECK-LABEL: define{{.*}} i32 @helper(
// CHECK: ret i32
// CHECK-LABEL: define{{.*}} i32 @untouched(
// CHECK: ret i32
