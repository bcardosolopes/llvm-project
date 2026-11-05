// RUN: %clang_cc1 -emit-cir -fclangir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=LLVM

// Verify we don't crash when multiple GNU local labels reuse the same source
// name in one function (e.g. two statement expressions with __label__ L).
// This regressed with an assertion in CIRGenModule::mapBlockAddress.

// CIR: cir.func{{.*}} @f
// CIR: cir.block_address <@f, "L$
// CIR: cir.block_address <@f, "L$
// LLVM: define{{.*}} @f
int f(int x) {
  void *a = ({ __label__ L; void *p = &&L; if (x) goto L; L: p; });
  void *b = ({ __label__ L; void *q = &&L; if (!x) goto L; L: q; });
  return a != b;
}
