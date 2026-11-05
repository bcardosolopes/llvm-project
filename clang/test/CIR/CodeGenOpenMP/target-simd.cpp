// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=HOST
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=HOSTLLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=OFFLOADLLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

int side();
long long step();

void target_simd_basic(int *p, int n) {
#pragma omp target simd map(tofrom : p[0:1]) if(target : n)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void target_simd_thread_limit_host_only(int *p, int n) {
#pragma omp target simd thread_limit(side())
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void target_simd_mixed_bounds(long *limit, int *p) {
#pragma omp target simd map(tofrom : p[0:1])
  for (int i = 0; i < *limit; ++i)
    p[0] += i;
}

void target_simd_nonunit_positive_step(int *p) {
#pragma omp target simd map(tofrom : p[0:1])
  for (int i = 3; i < 32; i += 5)
    p[0] += i;
}

void target_simd_descending_step(int *p) {
#pragma omp target simd map(tofrom : p[0:1])
  for (int i = 10; i > 1; --i)
    p[0] += i;
}

void target_simd_descending_inclusive(int *p) {
#pragma omp target simd map(tofrom : p[0:1])
  for (int i = 10; i >= 1; i -= 3)
    p[0] += i;
}

void target_simd_private_clause(double *p, int n) {
  double a = 1.0;
#pragma omp target simd map(tofrom : p[0:1]) if(target : n) private(a)
  for (int i = 0; i < 4; ++i) {
    a += i;
    p[0] += a;
  }
}

void target_simd_linear_clause(int *p) {
  long long k = step();
#pragma omp target simd map(tofrom : p[0:1]) linear(k : 3)
  for (int i = 0; i < 4; ++i)
    p[0] += i + k;
}

void target_simd_nontemporal(int *p, int *q) {
#pragma omp target simd map(tofrom : p[0:1], q[0:1]) nontemporal(p, q)
  for (int i = 0; i < 4; ++i)
    p[0] += q[0];
}

void target_simd_clause_bundle(float *p, int n) {
#pragma omp target simd map(tofrom : p[0:8]) if(simd : n + 1) simdlen(4) \
    safelen(8) aligned(p : 16) order(concurrent)
  for (int i = 0; i < 8; ++i)
    p[i] += 1.0f;
}

struct S {
  S(int *q) {
    int *p = q;
#pragma omp target simd map(tofrom : p[0:1])
    for (int i = 0; i < 4; ++i)
      p[0] += i;
  }
};

void ctor_target_simd(int *p) { S s(p); }

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_simd_basicPii(
// CIR: omp.target
// CIR: omp.simd {

// HOST-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_simd_thread_limit_host_onlyPii(
// HOST: cir.call @_Z4sidev
// HOST: omp.target thread_limit(%{{.*}} : i32)
// HOST: omp.simd {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_simd_mixed_boundsPlPi(
// CIR: omp.target
// CIR: cir.if %{{.*}} {
// CIR: omp.simd {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_simd_nonunit_positive_stepPi(
// CIR: omp.target
// CIR: omp.simd {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_simd_descending_stepPi(
// CIR: omp.target
// CIR: omp.simd {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_simd_descending_inclusivePi(
// CIR: omp.target
// CIR: omp.simd {
// CIR: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_simd_private_clausePdi(
// CIR: omp.target
// CIR: omp.simd {{.*}}private(

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_simd_linear_clausePi(
// CIR: omp.target
// CIR: omp.simd {{.*}}linear(

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_simd_nontemporalPiS_(
// CIR: omp.target
// CIR: omp.simd nontemporal(
// CIR: cir.load nontemporal

// CIR-LABEL: cir.func{{.*}} @_Z{{[0-9]+}}target_simd_clause_bundlePfi(
// CIR: omp.target
// CIR: omp.simd aligned(%{{.*}} : !cir.ptr<!cir.float> -> 16 : i64) if(%{{.*}}) order(concurrent) safelen(8) simdlen(4) {

// HOSTLLVM-LABEL: define dso_local void @_Z{{[0-9]+}}target_simd_thread_limit_host_onlyPii(
// HOSTLLVM: call{{.*}} @_Z4sidev
// HOSTLLVM: call void @__omp_offloading{{.*}}target_simd_thread_limit_host_only

// OFFLOADLLVM: @.__omp_offloading_{{.*}}_ZN1SC2EPi_l{{[0-9]+}}.region_id = weak constant i8 0
// OFFLOADLLVM-LABEL: define internal void @__omp_offloading_{{.*}}_ZN1SC2EPi_l{{[0-9]+}}(

// OGCG-LABEL: @_Z17target_simd_basicPii(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z34target_simd_thread_limit_host_onlyPii(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z24target_simd_mixed_boundsPlPi(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z33target_simd_nonunit_positive_stepPi(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z27target_simd_descending_stepPi(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z32target_simd_descending_inclusivePi(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z26target_simd_private_clausePdi(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z25target_simd_linear_clausePi(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z23target_simd_nontemporalPiS_(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z25target_simd_clause_bundlePfi(
// OGCG: call i32 @__tgt_target_kernel(
