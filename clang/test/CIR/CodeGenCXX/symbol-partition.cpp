// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -fsymbol-partition=part -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsymbol-partition=part -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// -fsymbol-partition should set partition on all definitions.
int gv = 42;
void fn() {}

// CIR: @gv = {{.*}} partition "part"
// CIR: define {{.*}} @_Z2fnv() {{.*}} partition "part"
// OGCG: @gv = {{.*}} partition "part"
// OGCG: define {{.*}} @_Z2fnv() {{.*}} partition "part"
