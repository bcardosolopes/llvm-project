// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Regression test for duplicate BlockAddrInfoAttr mappings when a label is
// nested in the else-arm of a short-circuit condition and also referenced by
// a goto in that arm.

int g(int a, int b, int c) {
  if (a && b)
    return 1;
  else {
    if (c)
      goto L;
L:
    return 0;
  }
}

// CIR-LABEL: cir.func {{.*}} @g
// CIR:       cir.label "L"
// CIR-NOT:   cir.label "L"

// LLVM-LABEL: define{{.*}} @g
// LLVM:       br label

// OGCG-LABEL: define{{.*}} @g
// OGCG:       br label
