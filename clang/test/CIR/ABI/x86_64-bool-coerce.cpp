// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll

// Bool field in struct return coercion was widened to i64 instead of
// i8. Fixed: GetINTEGERTypeAtOffset in X86.cpp now checks BoolType.

struct S {
  void *p;
  bool b;
};

// CHECK: define{{.*}} { ptr, i8 } @_Z4makev()
S make() { return {nullptr, true}; }
