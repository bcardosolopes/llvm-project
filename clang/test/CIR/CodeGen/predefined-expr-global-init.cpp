// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 -w %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that __PRETTY_FUNCTION__ in a global dynamic initializer
// doesn't crash with cast<FuncOp> assertion.

int test3(const char*);

// CHECK: @__PRETTY_FUNCTION__{{.*}} = private unnamed_addr constant [10 x i8] c"top level\00"
// CHECK: define internal void @__cxx_global_var_init()
// CHECK:   call noundef i32 @_Z5test3PKc(ptr noundef @__PRETTY_FUNCTION__
int test3g = test3(__PRETTY_FUNCTION__);
