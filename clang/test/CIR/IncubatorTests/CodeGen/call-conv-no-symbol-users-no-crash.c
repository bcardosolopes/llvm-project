// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

int leaf_add(int a, int b) { return a + b; }

// CHECK-LABEL: define{{.*}} i32 @leaf_add(
// CHECK: add nsw i32
// CHECK: ret i32
