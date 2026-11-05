// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o - | FileCheck %s

extern int helper(int);

int call_decl(int x) { return helper(x); }

// CHECK-LABEL: define{{.*}} i32 @call_decl(
// CHECK: call i32 @helper(i32
