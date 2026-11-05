// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fno-clangir-call-conv-lowering %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fno-clangir-call-conv-lowering %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Regression coverage for repeated long double va_arg accesses on x86_64.
// This keeps pressure on the memory-path layout metadata cache in
// LoweringPrepareX86CXXABI (alignment + rounded stride reused per type).

long double sum_ld(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  long double a = __builtin_va_arg(ap, long double);
  long double b = __builtin_va_arg(ap, long double);
  __builtin_va_end(ap);
  return a + b;
}

// CIR-LABEL: cir.func {{.*}} @sum_ld
// CIR: cir.va_start
// CIR: cir.va_end

// LLVM-LABEL: define {{.*}} @sum_ld
// LLVM: call void @llvm.va_start.p0
// LLVM: call void @llvm.va_end.p0
