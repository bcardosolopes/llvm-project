// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 -w %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that module-exported structured bindings don't crash CIR.
// Previously, BindingDecl was not handled in emitTopLevelDecl.

export module Foo;
int ary[2] = {10, 20};
export auto &[a, b] = ary;

// CHECK: @{{.*}}ary = global [2 x i32] [i32 10, i32 20]
