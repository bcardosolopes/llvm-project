// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o /dev/null 2>&1 | FileCheck %s

// The GNU array range designator extension with a non-constant-foldable filler
// (here a function call) is not yet implemented for aggregate initialization in
// ClangIR. Previously this hit an llvm_unreachable and crashed the compiler;
// emit a graceful diagnostic instead, matching classic CodeGen's
// ErrorUnsupported. Constant range designators are handled on the
// constant-folding path and still compile fine.

int g(void);
int test(void) {
  int a[6] = {[0 ... 4] = g()};
  return a[0];
}
// CHECK: ClangIR code gen Not Yet Implemented: GNU array range designator extension
