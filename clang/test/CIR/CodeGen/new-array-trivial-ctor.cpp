// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that CIR does not emit spurious constructor calls for trivially-copyable
// types with non-zero null representation (member pointers) in new[].

struct S {
  int S::*mem;
  int a;
};

void test(int n) {
  S *p = new S[n]();
}

// CIR-NOT: call {{.*}}_ZN1SC1Ev
// CIR-NOT: call {{.*}}_ZN1SC2Ev

// OGCG-NOT: call {{.*}}_ZN1SC1Ev
// OGCG-NOT: call {{.*}}_ZN1SC2Ev
