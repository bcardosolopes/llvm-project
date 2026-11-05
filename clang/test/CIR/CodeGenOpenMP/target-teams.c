// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=HOST-CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=OFFLOAD-CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=OFFLOAD-LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=45 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=OMP45-LLVM --implicit-check-not="__kmpc_set_thread_limit"
// RUN: %clang_cc1 -triple i386-unknown-unknown -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void work12(int, int);
int tick(void);

void foo(int q) {
  int p = 2;

  // HOST-CIR: omp.private {type = firstprivate}
  // HOST-CIR: cir.func{{.*}} @foo
  // HOST-CIR: omp.teams private(@{{.*}} %{{.*}} -> %arg1 : !cir.ptr<!s32i>) {
  // HOST-CIR: cir.call @work12

  // OFFLOAD-CIR-LABEL: cir.func{{.*}} @foo
  // OFFLOAD-CIR: omp.target map_entries(%{{.*}} -> %arg1 : !cir.ptr<!s32i>) private(@{{.*}} %{{.*}} -> %arg2 [map_idx=0] : !cir.ptr<!s32i>) {
  // OFFLOAD-CIR-NEXT: omp.teams private(@{{.*}} %arg2 -> %arg3 : !cir.ptr<!s32i>) {
  // OFFLOAD-CIR: cir.call @work12

  // OFFLOAD-LLVM-LABEL: define dso_local void @foo(
  // OFFLOAD-LLVM: call i32 @__tgt_target_kernel(
#pragma omp target teams firstprivate(p)
  work12(p, p);
}

void bar(int q, int n, int tl) {
  int p = 2;

  // HOST-CIR-LABEL: cir.func no_inline optnone dso_local @bar
  // HOST-CIR: %[[N_ADDR:.+]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["n", init]
  // HOST-CIR: %[[TL_ADDR:.+]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["tl", init]
  // HOST-CIR: %[[N_CAP:.+]] = cir.alloca !s32i, !cir.ptr<!s32i>, [".capture_expr."]
  // HOST-CIR: %[[TL_CAP:.+]] = cir.alloca !s32i, !cir.ptr<!s32i>, [".capture_expr."]
  // HOST-CIR: %[[N_VAL:.+]] = cir.load align(4) %[[N_ADDR]] : !cir.ptr<!s32i>, !s32i
  // HOST-CIR: cir.store align(4) %[[N_VAL]], %[[N_CAP]] : !s32i, !cir.ptr<!s32i>
  // HOST-CIR: %[[TL_VAL:.+]] = cir.load align(4) %[[TL_ADDR]] : !cir.ptr<!s32i>, !s32i
  // HOST-CIR: cir.store align(4) %[[TL_VAL]], %[[TL_CAP]] : !s32i, !cir.ptr<!s32i>
  // HOST-CIR: omp.teams num_teams( to %{{.+}} : i32) thread_limit(%{{.+}} : i32) private(@{{.*}} %{{.*}} -> %arg3 : !cir.ptr<!s32i>) {
  // HOST-CIR: cir.call @work12

  // OFFLOAD-CIR-LABEL: cir.func no_inline optnone dso_local @bar
  // OFFLOAD-CIR: omp.target thread_limit(%{{.*}} : i32) host_eval(%{{.*}} -> %arg3, %{{.*}} -> %arg4 : i32, i32) map_entries(%{{.*}} -> %arg5 : !cir.ptr<!s32i>) private(@{{.*}} %{{.*}} -> %arg6 [map_idx=0] : !cir.ptr<!s32i>) {
  // OFFLOAD-CIR-NEXT: omp.teams num_teams( to %arg3 : i32) thread_limit(%arg4 : i32) private(@{{.*}} %arg6 -> %arg7 : !cir.ptr<!s32i>) {
  // OFFLOAD-CIR: cir.call @work12

  // OFFLOAD-LLVM-LABEL: define dso_local void @bar(
  // OFFLOAD-LLVM: call i32 @__tgt_target_kernel(ptr @{{.*}}, i64 -1, i32 %{{.*}}, i32 %{{.*}},
  // OMP45-LLVM-LABEL: define internal void @__omp_offloading{{.*}}bar{{.*}}(
  // OMP45-LLVM: call void @__kmpc_push_num_teams(ptr @{{.*}}, i32 %{{.*}}, i32 %{{.*}}, i32 %{{.*}})
  // OMP45-LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @{{.*}}, i32 1, ptr @__omp_offloading{{.*}}bar{{.*}}.omp_par
#pragma omp target teams firstprivate(p) num_teams(n) thread_limit(tl)
  work12(p, p);
}

void baz(int q) {
  int p = 2;

  // HOST-CIR-LABEL: cir.func no_inline optnone dso_local @baz
  // HOST-CIR: omp.teams private(@{{.*}} %{{.*}} -> %arg1 : !cir.ptr<!s32i>) {
  // HOST-CIR: cir.call @work12

  // OFFLOAD-CIR-LABEL: cir.func no_inline optnone dso_local @baz
  // OFFLOAD-CIR: omp.target if(%{{.*}}) map_entries(%{{.*}} -> %arg1 : !cir.ptr<!s32i>) private(@{{.*}} %{{.*}} -> %arg2 [map_idx=0] : !cir.ptr<!s32i>) {
  // OFFLOAD-CIR-NEXT: omp.teams private(@{{.*}} %arg2 -> %arg3 : !cir.ptr<!s32i>) {
  // OFFLOAD-CIR: cir.call @work12
#pragma omp target teams if(q > 0) firstprivate(p)
  work12(p, p);
}

void qux(int q) {
  int p = 2;

  // HOST-CIR-LABEL: cir.func no_inline optnone dso_local @qux
  // HOST-CIR: omp.teams private(@{{.*}} %{{.*}} -> %arg1 : !cir.ptr<!s32i>) {
  // HOST-CIR: cir.call @work12

  // OFFLOAD-CIR-LABEL: cir.func no_inline optnone dso_local @qux
  // OFFLOAD-CIR: omp.target if(%{{.*}}) map_entries(%{{.*}} -> %arg1 : !cir.ptr<!s32i>) private(@{{.*}} %{{.*}} -> %arg2 [map_idx=0] : !cir.ptr<!s32i>) {
  // OFFLOAD-CIR-NEXT: omp.teams private(@{{.*}} %arg2 -> %arg3 : !cir.ptr<!s32i>) {
  // OFFLOAD-CIR: cir.call @work12
#pragma omp target teams if(target: q > 0) firstprivate(p)
  work12(p, p);
}

void quux(int q) {
  int p = 2;

  // HOST-CIR-LABEL: cir.func no_inline optnone dso_local @quux
  // HOST-CIR: omp.teams private(@{{.*}} %{{.*}} -> %arg1 : !cir.ptr<!s32i>) {
  // HOST-CIR: cir.call @work12

  // OFFLOAD-CIR-LABEL: cir.func no_inline optnone dso_local @quux
  // OFFLOAD-CIR: omp.target device(%{{.*}} : i32) map_entries(%{{.*}} -> %arg1 : !cir.ptr<!s32i>) private(@{{.*}} %{{.*}} -> %arg2 [map_idx=0] : !cir.ptr<!s32i>) {
  // OFFLOAD-CIR-NEXT: omp.teams private(@{{.*}} %arg2 -> %arg3 : !cir.ptr<!s32i>) {
  // OFFLOAD-CIR: cir.call @work12

  // OFFLOAD-LLVM-LABEL: define dso_local void @quux(
  // OFFLOAD-LLVM: call i32 @__tgt_target_kernel(ptr @{{.*}}, i64 %{{.*}}, i32 0, i32 0,
  // OFFLOAD-LLVM-LABEL: define internal void @__omp_offloading{{.*}}quux{{.*}}(i64 noundef %0)
  // OFFLOAD-LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @{{.*}}, i32 1, ptr @__omp_offloading{{.*}}quux{{.*}}.omp_par, i32 %{{.*}})
#pragma omp target teams device(q) firstprivate(p)
  work12(p, p);
}

void corge(void) {
  int p = 2;

  // HOST-CIR-LABEL: cir.func no_inline optnone dso_local @corge()
  // HOST-CIR: cir.call @tick
  // HOST-CIR: omp.teams private(@{{.*}} %{{.*}} -> %arg0 : !cir.ptr<!s32i>) {
  // HOST-CIR: cir.call @work12

  // HOST-LLVM-LABEL: define dso_local void @corge()
  // HOST-LLVM: call i32 @tick()
  // HOST-LLVM: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @{{.*}}, i32 1, ptr @corge..omp_par, i32 %{{.*}})
#pragma omp target teams device(tick()) firstprivate(p)
  work12(p, p);
}

void grault(void) {
  int p = 2;
  int sum = 0;

  // HOST-CIR-LABEL: cir.func no_inline optnone dso_local @grault()
  // HOST-CIR: omp.teams
  // HOST-CIR-SAME: private(
  // HOST-CIR-SAME: reduction(

  // OFFLOAD-CIR-LABEL: cir.func no_inline optnone dso_local @grault()
  // OFFLOAD-CIR: %[[SUM_MAP:.*]] = omp.map.info {{.*}} map_clauses(tofrom) {{.*}} {name = "sum"}
  // OFFLOAD-CIR: omp.target map_entries(
  // OFFLOAD-CIR-SAME: %[[SUM_MAP]] -> %arg{{[0-9]+}} : !cir.ptr<!s32i>
  // OFFLOAD-CIR: omp.teams
  // OFFLOAD-CIR-SAME: private(
  // OFFLOAD-CIR-SAME: reduction(
#pragma omp target teams firstprivate(p) reduction(+ : sum)
  {
    sum += p;
  }

  work12(sum, p);
}

// OGCG-LABEL: define dso_local void @foo(
// OGCG: call void @__omp_offloading_{{.*}}_foo_{{.*}}(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_foo_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(

// OGCG-LABEL: define dso_local void @bar(
// OGCG: call void @__omp_offloading_{{.*}}_bar_{{.*}}(

// OGCG-LABEL: define internal void @__omp_offloading_{{.*}}_bar_{{.*}}(
// OGCG: call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(

// OGCG-LABEL: define dso_local void @baz(
// OGCG: call void @__omp_offloading_{{.*}}_baz_{{.*}}(

// OGCG-LABEL: define dso_local void @qux(
// OGCG: call void @__omp_offloading_{{.*}}_qux_{{.*}}(

// OGCG-LABEL: define dso_local void @corge(
// OGCG: call i32 @tick()
// OGCG: call void @__omp_offloading_{{.*}}_corge_{{.*}}(

// OGCG-LABEL: define dso_local void @grault(
// OGCG: call void @__omp_offloading_{{.*}}_grault_{{.*}}(
