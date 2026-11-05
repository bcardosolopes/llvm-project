// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=45 -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=45 -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-version=45 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void taskloop_simd_descending_nonunit(int *p) {
#pragma omp taskloop simd
  for (int i = 15; i >= 3; i -= 4)
    p[0] += i;
}

void taskloop_simd_collapse(int argc, char **argv) {
  int i;
#pragma omp taskloop simd if(argc) shared(argc, argv) collapse(2) num_tasks(4) \
    safelen(32)
  for (i = 0; i < argc; ++i)
    for (int j = argc; j < argv[argc][argc]; ++j)
      ;
}

struct S {
  int a;
  void member_iv(int c);
};

void S::member_iv(int c) {
#pragma omp taskloop simd shared(c) num_tasks(a) simdlen(8) safelen(64)
  for (a = 0; a < c; ++a)
    ;
}

// CIR-LABEL: cir.func{{.*}} @_Z32taskloop_simd_descending_nonunitPi(
// CIR: omp.taskloop
// CIR: omp.simd
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {
// CIR: cir.binop(mul, %{{.*}}, %{{.*}}) nsw : !s32i
// CIR: cir.binop(sub, %{{.*}}, %{{.*}}) nsw : !s32i

// CIR-LABEL: cir.func{{.*}} @_Z22taskloop_simd_collapseiPPc(
// CIR: cir.if %{{.*}} {
// CIR: omp.taskloop if(%{{.*}}) num_tasks(%{{.*}}: i32) {
// CIR: omp.simd safelen(32) {
// CIR: omp.loop_nest (%{{.*}}) : i64 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_ZN1S9member_ivEi(
// CIR: cir.get_member %{{.*}}[0] {name = "a"}
// CIR: omp.taskloop num_tasks(%{{.*}}: i32) {
// CIR: omp.simd safelen(64) simdlen(8) {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// OGCG-LABEL: define {{.*}} @_Z32taskloop_simd_descending_nonunitPi(
// OGCG: call void @__kmpc_taskloop(

// OGCG-LABEL: define {{.*}} @_Z22taskloop_simd_collapseiPPc(
// OGCG: call void @__kmpc_taskloop(

// OGCG-LABEL: define {{.*}} @_ZN1S9member_ivEi(
// OGCG: call void @__kmpc_taskloop(
