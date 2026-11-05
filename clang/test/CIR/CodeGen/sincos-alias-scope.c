// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// Sincos stores should have alias.scope/noalias metadata.

void sincosf(float, float*, float*);

void test(float x) {
  float s, c;
  __builtin_sincosf(x, &s, &c);
}

// CIR: store float {{.*}}, ptr {{.*}}, !alias.scope [[SCOPE:![0-9]+]]
// CIR: store float {{.*}}, ptr {{.*}}, !noalias [[SCOPE]]
// OGCG: store float {{.*}}, ptr {{.*}}, !alias.scope [[SCOPE:![0-9]+]]
// OGCG: store float {{.*}}, ptr {{.*}}, !noalias [[SCOPE]]
