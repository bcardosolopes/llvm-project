// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s -check-prefix=OGCG

int rel_lt(float a, float b) { return a < b; }
int eq_cmp(float a, float b) { return a == b; }

// CIR-LABEL: cir.func{{.*}} @rel_lt(
// CIR: cir.cmp(lt, {{.*}}) : !cir.float, !cir.bool

// CIR-LABEL: cir.func{{.*}} @eq_cmp(
// CIR: cir.cmp(eq, {{.*}}) : !cir.float, !cir.bool

// LLVM-LABEL: define{{.*}} i32 @rel_lt(
// LLVM: fcmp olt float

// LLVM-LABEL: define{{.*}} i32 @eq_cmp(
// LLVM: fcmp oeq float

// OGCG-LABEL: define{{.*}} i32 @rel_lt(
// OGCG: fcmp olt float

// OGCG-LABEL: define{{.*}} i32 @eq_cmp(
// OGCG: fcmp oeq float
