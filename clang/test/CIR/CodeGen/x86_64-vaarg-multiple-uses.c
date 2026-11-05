// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fno-clangir-call-conv-lowering %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fno-clangir-call-conv-lowering %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Regression coverage for repeated x86_64 va_arg lowering in a single
// function. This exercises multiple lowerVAArgX86_64 calls within one module,
// which now reuses a cached LowerModule instead of rebuilding it each time.

int sum_two(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  int a = __builtin_va_arg(ap, int);
  int b = __builtin_va_arg(ap, int);
  __builtin_va_end(ap);
  return a + b;
}

// CIR-LABEL: cir.func {{.*}} @sum_two
// CIR: cir.va_start
// CIR: cir.scope
// CIR: cir.scope
// CIR: cir.va_end

// LLVM-LABEL: define {{.*}} @sum_two
// LLVM: call void @llvm.va_start.p0
// LLVM: call void @llvm.va_end.p0
// LLVM: add nsw i32
