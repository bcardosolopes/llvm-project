// RUN: %clang_cc1 -fopenmp -x c++ -triple x86_64-unknown-unknown \
// RUN:   -fopenmp-targets=amdgcn-amd-amdhsa -fclangir -emit-llvm-bc \
// RUN:   -verify %s -o %t.bc

// A lastprivate clause on an offloaded distribute combined construct would reuse
// the host_eval loop upper-bound value for its last-iteration check, which the
// OpenMP dialect verifier rejects ("host_eval argument illegal use"). This is
// diagnosed as not-yet-implemented instead of producing invalid IR; without the
// fix it aborted the compile with a "Broken module found" backend error.

void f(int *a) {
  int j;
  // expected-error@+1 {{ClangIR code gen Not Yet Implemented: OpenMP OMPTargetTeamsDistributeParallelForDirective lastprivate with host-evaluated loop bounds}}
#pragma omp target teams distribute parallel for lastprivate(j) map(tofrom: a[0:10])
  for (int i = 0; i < 10; i++) {
    j = i;
    a[i] = j;
  }
}
