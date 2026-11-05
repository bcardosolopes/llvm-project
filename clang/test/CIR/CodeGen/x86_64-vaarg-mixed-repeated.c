// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fno-clangir-call-conv-lowering %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fno-clangir-call-conv-lowering %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Regression coverage for repeated mixed-type va_arg classification on x86_64.
// This exercises repeated X86_64ABIInfo-driven classification in one function.
// The pattern intentionally repeats int/double retrieval to hit the per-type
// classification cache in LoweringPrepareX86CXXABI.

double sum_mixed(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  int i1 = __builtin_va_arg(ap, int);
  double d1 = __builtin_va_arg(ap, double);
  int i2 = __builtin_va_arg(ap, int);
  double d2 = __builtin_va_arg(ap, double);
  __builtin_va_end(ap);
  return (double)(i1 + i2) + d1 + d2;
}

// CIR-LABEL: cir.func {{.*}} @sum_mixed
// CIR: cir.va_start
// CIR: cir.scope
// CIR: cir.scope
// CIR: cir.scope
// CIR: cir.scope
// CIR: cir.va_end

// LLVM-LABEL: define {{.*}} @sum_mixed
// LLVM: call void @llvm.va_start.p0
// LLVM: call void @llvm.va_end.p0
