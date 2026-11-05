// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -ffast-math -fclangir \
// RUN:   -O2 -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -ffast-math -fclangir \
// RUN:   -O2 -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -ffast-math \
// RUN:   -O2 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s
//
// Under fast-math the fast-math flags must flow onto the ternary/select result,
// not just the comparison. A conditional whose arms fold to a cir.select must
// carry the flags on that select so the lowered LLVM `select` is tagged
// `fast`, matching classic codegen (and, e.g., unblocking fcmp+select ->
// llvm.maxnum/minnum folds that depend on fast-math flags on the select).

// Constant arms: CIRGen builds a cir.ternary, CIRSimplify folds it to a
// cir.select which must carry the fast-math flags.
float pickf(float a) { return a > 1.0f ? 2.0f : 3.0f; }

// CIR-LABEL: cir.func {{.*}}@pickf
// CIR: %[[CMP:.*]] = cir.cmp(gt, {{.*}}, {{.*}}) nnan|ninf|nsz|arcp|afn|reassoc : !cir.float, !cir.bool
// CIR: cir.select if %[[CMP]] then {{.*}} else {{.*}} nnan|ninf|nsz|arcp|afn|reassoc : (!cir.bool, !cir.float, !cir.float) -> !cir.float

// LLVM-LABEL: define {{.*}}float @pickf
// LLVM: fcmp reassoc nnan ninf nsz arcp afn ogt float
// LLVM: select reassoc nnan ninf nsz arcp afn i1

// OGCG-LABEL: define {{.*}}float @pickf
// OGCG: fcmp reassoc nnan ninf nsz arcp afn ogt float
// OGCG: select reassoc nnan ninf nsz arcp afn i1

double pickd(double a) { return a < 1.0 ? 2.0 : 3.0; }

// CIR-LABEL: cir.func {{.*}}@pickd
// CIR: cir.select if {{.*}} then {{.*}} else {{.*}} nnan|ninf|nsz|arcp|afn|reassoc : (!cir.bool, !cir.double, !cir.double) -> !cir.double

// LLVM-LABEL: define {{.*}}double @pickd
// LLVM: select reassoc nnan ninf nsz arcp afn i1

// OGCG-LABEL: define {{.*}}double @pickd
// OGCG: select reassoc nnan ninf nsz arcp afn i1

// Integer selects must be unaffected: no fast-math flags on the select.
int picki(int a) { return a > 1 ? 2 : 3; }

// CIR-LABEL: cir.func {{.*}}@picki
// CIR: cir.select if {{.*}} then {{.*}} else {{.*}} : (!cir.bool, !s32i, !s32i) -> !s32i
// CIR-NOT: cir.select {{.*}} reassoc

// LLVM-LABEL: define {{.*}}i32 @picki
// LLVM: select i1
// LLVM-NOT: select reassoc
