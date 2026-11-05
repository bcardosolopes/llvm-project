// A GNU computed goto (labels-as-values, `goto *p`) whose dispatch and all
// target labels live inside an OpenMP parallel region. The cir.indirect_goto
// must be resolved to a cir.indirect_br within the omp.parallel region -- the
// dispatch block CIRGen creates in the function body cannot be branched to
// across the region boundary -- and the region must still outline, with the
// jump-table blockaddresses re-pointed at the outlined function.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir \
// RUN:   -emit-mlir=cir-flat %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIRFLAT
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s \
// RUN:   -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

void f(void) {
#pragma omp parallel num_threads(2)
  {
    static void *tbl[] = {&&A, &&B, &&C};
    int i = 0;
    goto *tbl[i];
  A:
    i = 1;
    goto *tbl[i];
  B:
    i = 2;
    goto *tbl[i];
  C:;
  }
}

// Every indirect goto is resolved inside the omp.parallel region (no residual
// cir.indirect_goto) and the region-local dispatch cir.indirect_br lists the
// three label blocks as successors.
// CIRFLAT-LABEL: cir.func {{.*}} @f
// CIRFLAT:         omp.parallel
// CIRFLAT-NOT:     cir.indirect_goto
// CIRFLAT:         cir.label "A"
// CIRFLAT-NOT:     cir.indirect_goto
// CIRFLAT:         cir.label "B"
// CIRFLAT-NOT:     cir.indirect_goto
// CIRFLAT:         cir.label "C"
// CIRFLAT-NOT:     cir.indirect_goto
// CIRFLAT:         cir.indirect_br {{.*}} : !cir.ptr<!void>, [
// CIRFLAT-NEXT:    ^bb{{[0-9]+}},
// CIRFLAT-NEXT:    ^bb{{[0-9]+}},
// CIRFLAT-NEXT:    ^bb{{[0-9]+}}

// The parallel region outlines and the jump table's blockaddresses point at the
// outlined function, whose body holds the indirectbr over the three labels.
// LLVM: @f.tbl = internal global {{.*}} [ptr blockaddress(@f..omp_par,
// LLVM-LABEL: define internal void @f..omp_par
// LLVM:         indirectbr ptr %{{.*}}, [label %{{.*}}, label %{{.*}}, label %{{.*}}]

// OGCG: @f.tbl = internal global {{.*}} [ptr blockaddress(@f.omp_outlined,
// OGCG-LABEL: define internal void @f.omp_outlined
// OGCG:         indirectbr ptr %{{.*}}, [label %{{.*}}, label %{{.*}}, label %{{.*}}]
