// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

struct S {
  int x;
  int y;
};

// NonAtomicToAtomic cast through aggregate initialization.
// For non-padded atomic types, this is a no-op (just copies the value).
void init_atomic(S s) {
  _Atomic(S) a = s;
}

// CIR: cir.func {{.*}} @_Z11init_atomic1S
// CIR:   cir.copy

// LLVM: define {{.*}} void @_Z11init_atomic1S
// LLVM:   call void @llvm.memcpy
