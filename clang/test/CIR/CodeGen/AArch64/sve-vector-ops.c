// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64 -target-feature +sve -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64 -target-feature +sve -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

#include <arm_sve.h>

int f_subscript(svint32_t v, int i) {
  return v[i];
}

// LLVM-LABEL: define dso_local i32 @f_subscript
// LLVM: extractelement <vscale x 4 x i32>
