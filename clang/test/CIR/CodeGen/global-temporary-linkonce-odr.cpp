// RUN: %clang_cc1 -std=c++14 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that variable template reference temporaries get linkonce_odr linkage
// with correct (non-private) MLIR visibility.

namespace VariableTemplateWithConstRef {
  template <typename T>
  const int &i = 5;
  const int &use = i<void>;
}

// The materialized temporary for 'i<void>' should be linkonce_odr (not
// internal) and must NOT have private MLIR visibility (which would cause
// a verification error).
// CHECK: cir.global  constant linkonce_odr comdat  @_ZGRN28VariableTemplateWithConstRef1iIvEE_ = #cir.int<5> : !s32i
