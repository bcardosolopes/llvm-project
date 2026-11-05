// RUN: %clang_cc1 -fopenmp -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void before(int);
void during(int);
void after(int);

void emit_simple_parallel() {
  // CHECK: cir.func{{.*}}@emit_simple_parallel
  int i = 5;
  before(i);
  // CHECK: %[[I_LOAD:.*]] = cir.load{{.*}}
  // CHECK-NEXT: cir.call @before(%[[I_LOAD]])

#pragma omp parallel
  {}
  // CHECK-NEXT: omp.parallel {
  // CHECK-NEXT: omp.terminator
  // CHECK-NEXT: }
#pragma omp parallel
  {
    during(i);
  }
  // CHECK-NEXT: omp.parallel {
  // CHECK:      cir.call @during
  // CHECK:      omp.terminator
  // CHECK-NEXT: }

  after(i);
  // CHECK: %[[I_LOAD:.*]] = cir.load{{.*}}
  // CHECK-NEXT: cir.call @after(%[[I_LOAD]])
}

// OGCG-LABEL: define {{.*}} @emit_simple_parallel()
// OGCG: call void @before(i32 noundef %{{.*}})
// OGCG: call void {{.*}} @__kmpc_fork_call({{.*}}, i32 0, ptr @emit_simple_parallel.omp_outlined)
// OGCG: call void {{.*}} @__kmpc_fork_call({{.*}}, i32 1, ptr @emit_simple_parallel.omp_outlined.1,
// OGCG: call void @after(i32 noundef %{{.*}})
// OGCG-LABEL: define internal void @emit_simple_parallel.omp_outlined(
// OGCG-NEXT: entry:
// OGCG: ret void
// OGCG-LABEL: define internal void @emit_simple_parallel.omp_outlined.1(
// OGCG: call void @during(i32 noundef %{{.*}})

void proc_bind_parallel() {
  // CHECK: cir.func{{.*}}@proc_bind_parallel
#pragma omp parallel proc_bind(master)
  {}
  // CHECK-NEXT: omp.parallel proc_bind(master) {
  // CHECK-NEXT: omp.terminator
  // CHECK-NEXT: }
#pragma omp parallel proc_bind(close)
  {}
  // CHECK-NEXT: omp.parallel proc_bind(close) {
  // CHECK-NEXT: omp.terminator
  // CHECK-NEXT: }
#pragma omp parallel proc_bind(spread)
  {}
  // CHECK-NEXT: omp.parallel proc_bind(spread) {
  // CHECK-NEXT: omp.terminator
  // CHECK-NEXT: }
#pragma omp parallel proc_bind(primary)
  {}
  // CHECK-NEXT: omp.parallel proc_bind(primary) {
  // CHECK-NEXT: omp.terminator
  // CHECK-NEXT: }
#pragma omp parallel proc_bind(default)
  {}
  // CHECK-NEXT: omp.parallel {
  // CHECK-NEXT: omp.terminator
  // CHECK-NEXT: }
}

// OGCG-LABEL: define {{.*}} @proc_bind_parallel()
// OGCG: %[[PB_GTID:.*]] = call i32 @__kmpc_global_thread_num(
// OGCG: call void @__kmpc_push_proc_bind({{.*}}, i32 %[[PB_GTID]], i32 2)
// OGCG: call void {{.*}} @__kmpc_fork_call(
// OGCG: call void @__kmpc_push_proc_bind({{.*}}, i32 %[[PB_GTID]], i32 3)
// OGCG: call void {{.*}} @__kmpc_fork_call(
// OGCG: call void @__kmpc_push_proc_bind({{.*}}, i32 %[[PB_GTID]], i32 4)
// OGCG: call void {{.*}} @__kmpc_fork_call(
// OGCG: call void @__kmpc_push_proc_bind({{.*}}, i32 %[[PB_GTID]], i32 5)
// OGCG: call void {{.*}} @__kmpc_fork_call(
// OGCG: call void @__kmpc_push_proc_bind({{.*}}, i32 %[[PB_GTID]], i32 6)
// OGCG: call void {{.*}} @__kmpc_fork_call(

void if_parallel() {
  // CHECK: cir.func{{.*}}@if_parallel
  int i = 1;
#pragma omp parallel if(i)
  {
    during(i);
  }
  // CHECK: omp.parallel if(%{{.*}}) {
  // CHECK: cir.call @during
  // CHECK: omp.terminator
}

// OGCG-LABEL: define {{.*}} @if_parallel()
// OGCG: %[[IF_GTID:.*]] = call i32 @__kmpc_global_thread_num(
// OGCG: br i1 %{{.*}}, label %[[IF_THEN:.*]], label %[[IF_ELSE:.*]]
// OGCG: call void {{.*}} @__kmpc_fork_call(
// OGCG: call void @__kmpc_serialized_parallel(
// OGCG: call void @__kmpc_end_serialized_parallel(

void shared_parallel(void) {
  // CHECK: cir.func{{.*}}@shared_parallel
  int x = 0;
#pragma omp parallel shared(x)
  {
    during(x);
  }
  // CHECK: omp.parallel {
  // CHECK: cir.call @during
  // CHECK: omp.terminator
}

// LLVM-LABEL: define {{.*}} @emit_simple_parallel()
// LLVM:         call void @before(
// LLVM:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// LLVM:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// LLVM:         call void @after(

// LLVM-LABEL: define {{.*}} @proc_bind_parallel()
// LLVM:         call void @__kmpc_push_proc_bind(
// LLVM:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(

// LLVM-LABEL: define {{.*}} @if_parallel()
// LLVM:         call void @__kmpc_fork_call_if(

// LLVM-LABEL: define {{.*}} @shared_parallel()
// LLVM:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(

// OGCG-LABEL: define {{.*}} @shared_parallel()
// OGCG: call void {{.*}} @__kmpc_fork_call({{.*}}, i32 1, ptr @shared_parallel.omp_outlined,
// OGCG-LABEL: define internal void @shared_parallel.omp_outlined(
// OGCG: call void @during(i32 noundef %{{.*}})
