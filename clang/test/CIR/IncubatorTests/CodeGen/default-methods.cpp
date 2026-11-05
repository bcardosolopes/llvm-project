// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Trivial implicit operator= functions are lowered directly to cir.copy.
struct S {
  struct T {
    int x;
  } t;
};

// CIR-LABEL: cir.func {{.*}} @_Z1fR1SS0_(
// CIR:         cir.copy

// LLVM-LABEL: define dso_local void @_Z1fR1SS0_(
// LLVM:         call void @llvm.memcpy
void f(S &s1, S &s2) { s1 = s2; }
