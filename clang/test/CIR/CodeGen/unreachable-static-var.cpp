// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.og.ll

// Unreachable static variable after return should not emit guard
// variable or __cxa_atexit registration code.

struct A { ~A(); };

void func() {
  return;
  static A k; // unreachable — should not emit guard/atexit
}

// LLVM: define {{.*}} @_Z4funcv()
// LLVM-NEXT:   ret void
// LLVM-NEXT: }

// OGCG: define {{.*}} @_Z4funcv()
// OGCG:   ret void
// OGCG-NEXT: }
