// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

extern int foo(int);

void parallel_basic(int *p, int n, int teams, int threads) {
#pragma omp target teams loop map(tofrom : p[0:1]) if(target : n) \
    num_teams(teams) thread_limit(threads) firstprivate(n)
  for (int i = 0; i < 4; ++i)
    p[0] += i + n;
}

void generated_basic(int *p, int n, int teams, int threads) {
#pragma omp target teams loop map(tofrom : p[0:1]) if(target : n) \
    num_teams(teams) thread_limit(threads) firstprivate(n)
  for (int i = 0; i != n; ++i)
    p[0] += i;
}

void distribute_basic(int *p, int n, int teams, int threads) {
#pragma omp target teams loop map(tofrom : p[0:1]) if(target : n) \
    num_teams(teams) thread_limit(threads) firstprivate(n)
  for (int i = 0; i < 4; ++i)
    p[0] += foo(i) + n;
}

void distribute_order(int *p, int n, int teams, int threads) {
#pragma omp target teams loop order(concurrent) map(tofrom : p[0:1]) if(target : n) \
    num_teams(teams) thread_limit(threads) firstprivate(n)
  for (int i = 0; i < 4; ++i)
    p[0] += foo(i) + n;
}

void parallel_reduction(int *p, int n, int teams, int threads) {
  int sum = 0;
#pragma omp target teams loop reduction(+ : sum) map(tofrom : p[0:1]) if(target : n) \
    num_teams(teams) thread_limit(threads) firstprivate(n)
  for (int i = 0; i < 4; ++i)
    sum += i + n;
  p[0] = sum;
}

void distribute_reduction(int *p, int n, int teams, int threads) {
  int sum = 0;
#pragma omp target teams loop reduction(+ : sum) map(tofrom : p[0:1]) if(target : n) \
    num_teams(teams) thread_limit(threads) firstprivate(n)
  for (int i = 0; i < 4; ++i)
    sum += foo(i) + n;
  p[0] = sum;
}

// CIR-LABEL: cir.func{{.*}} @_Z14parallel_basicPiiii(
// CIR: omp.target if(%{{.*}}) thread_limit(%{{.*}} : i32)
// CIR: omp.teams
// CIR-SAME: num_teams(
// CIR-SAME: thread_limit(
// CIR: omp.parallel
// CIR: omp.distribute
// CIR: omp.wsloop
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z15generated_basicPiiii(
// CIR: omp.target if(%{{.*}}) thread_limit(%{{.*}} : i32)
// CIR: omp.teams
// CIR-SAME: num_teams(
// CIR-SAME: thread_limit(
// CIR: omp.parallel
// CIR: omp.distribute
// CIR: omp.wsloop
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z16distribute_basicPiiii(
// CIR: omp.target if(%{{.*}}) thread_limit(%{{.*}} : i32)
// CIR: omp.teams
// CIR-SAME: num_teams(
// CIR-SAME: thread_limit(
// CIR-NOT: omp.parallel
// CIR: omp.distribute
// CIR-NOT: omp.wsloop
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z16distribute_orderPiiii(
// CIR: omp.target if(%{{.*}}) thread_limit(%{{.*}} : i32)
// CIR: omp.teams
// CIR-SAME: num_teams(
// CIR-SAME: thread_limit(
// CIR-NOT: omp.parallel
// CIR: omp.distribute order(concurrent)
// CIR-NOT: omp.wsloop
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z18parallel_reductionPiiii(
// CIR: %[[PAR_SUM_MAP:.*]] = omp.map.info {{.*}} map_clauses(tofrom) {{.*}} {name = "sum"}
// CIR: omp.target if(%{{.*}}) thread_limit(%{{.*}} : i32)
// CIR-SAME: map_entries(
// CIR-SAME: %[[PAR_SUM_MAP]]
// CIR: omp.teams
// CIR-SAME: num_teams(
// CIR-SAME: thread_limit(
// CIR-SAME: reduction(
// CIR: omp.parallel
// CIR: omp.distribute
// CIR: omp.wsloop
// CIR-SAME: reduction(
// CIR: omp.loop_nest

// CIR-LABEL: cir.func{{.*}} @_Z20distribute_reductionPiiii(
// CIR: %[[DIST_SUM_MAP:.*]] = omp.map.info {{.*}} map_clauses(tofrom) {{.*}} {name = "sum"}
// CIR: omp.target if(%{{.*}}) thread_limit(%{{.*}} : i32)
// CIR-SAME: map_entries(
// CIR-SAME: %[[DIST_SUM_MAP]]
// CIR: omp.teams
// CIR-SAME: num_teams(
// CIR-SAME: thread_limit(
// CIR-SAME: reduction(
// CIR-NOT: omp.parallel
// CIR: omp.distribute
// CIR-NOT: omp.wsloop
// CIR: omp.loop_nest

// OGCG-LABEL: @_Z14parallel_basicPiiii(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z15generated_basicPiiii(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z16distribute_basicPiiii(
// OGCG: call i32 @__tgt_target_kernel(

