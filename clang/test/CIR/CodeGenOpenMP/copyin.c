// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR-TLS
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM-TLS
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fnoopenmp-use-tls -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR-OMP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fnoopenmp-use-tls -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM-OMP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG-TLS
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fnoopenmp-use-tls -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG-OMP

#define N 100

int x;
#pragma omp threadprivate(x)

void test_omp_parallel_copyin(int *a) {
  x = 1;

#pragma omp parallel copyin(x)
#pragma omp for
  for (int i = 0; i < N; i++)
    a[i] = i + x;
}

void test_omp_parallel_for_copyin(int *a) {
  x = 2;

#pragma omp parallel for copyin(x)
  for (int i = 0; i < N; i++)
    a[i] = i + x;
}

void test_omp_parallel_for_simd_copyin(int *a) {
  x = 3;

#pragma omp parallel for simd copyin(x)
  for (int i = 0; i < N; i++)
    a[i] = i + x;
}

void test_omp_parallel_sections_copyin(int *a, int *b) {
  x = 4;

#pragma omp parallel sections copyin(x)
  {
#pragma omp section
    { *a = x; }

#pragma omp section
    { *b = x; }
  }
}

void test_omp_parallel_master_copyin(int *a) {
  x = 5;

#pragma omp parallel master copyin(x)
  for (int i = 0; i < N; i++)
    a[i] = i + x;
}

void test_omp_parallel_copyin_default_none(void) {
  x = 6;

#pragma omp parallel copyin(x) default(none)
  ++x;
}

void test_omp_parallel_copyin_static_local(void) {
  static int s = 7;
#pragma omp threadprivate(s)

#pragma omp parallel copyin(s)
  ++s;
}

// CIR-TLS-LABEL: cir.func{{.*}} @test_omp_parallel_copyin(
// CIR-TLS: %[[INIT:.*]] = cir.get_global thread_local @x : !cir.ptr<!s32i>
// CIR-TLS: cir.store{{.*}} %{{.*}}, %[[INIT]] : !s32i, !cir.ptr<!s32i>
// CIR-TLS: %[[MASTER:.*]] = cir.get_global thread_local @x : !cir.ptr<!s32i>
// CIR-TLS: omp.parallel {
// CIR-TLS: %[[PRIVATE:.*]] = cir.get_global thread_local @x : !cir.ptr<!s32i>
// CIR-TLS: %[[NE:.*]] = cir.cmp(ne, %[[MASTER]], %[[PRIVATE]]) : !cir.ptr<!s32i>, !cir.bool
// CIR-TLS: cir.if %[[NE]] {
// CIR-TLS: %[[VAL:.*]] = cir.load align(4) %[[MASTER]] : !cir.ptr<!s32i>, !s32i
// CIR-TLS: cir.store align(4) %[[VAL]], %[[PRIVATE]] : !s32i, !cir.ptr<!s32i>
// CIR-TLS: }
// CIR-TLS: omp.barrier
// CIR-TLS: omp.wsloop {

// CIR-TLS-LABEL: cir.func{{.*}} @test_omp_parallel_for_copyin(
// CIR-TLS: omp.parallel private(
// CIR-TLS: cir.if
// CIR-TLS: omp.barrier
// CIR-TLS: omp.wsloop nowait {

// CIR-TLS-LABEL: cir.func{{.*}} @test_omp_parallel_for_simd_copyin(
// CIR-TLS: omp.parallel private(
// CIR-TLS: cir.if
// CIR-TLS: omp.barrier
// CIR-TLS: omp.wsloop nowait {
// CIR-TLS: omp.simd {

// CIR-TLS-LABEL: cir.func{{.*}} @test_omp_parallel_sections_copyin(
// CIR-TLS: omp.parallel {
// CIR-TLS: cir.if
// CIR-TLS: omp.barrier
// CIR-TLS: omp.sections nowait {

// CIR-TLS-LABEL: cir.func{{.*}} @test_omp_parallel_master_copyin(
// CIR-TLS: omp.parallel {
// CIR-TLS: cir.if
// CIR-TLS: omp.barrier
// CIR-TLS: omp.master {

// CIR-TLS-LABEL: cir.func{{.*}} @test_omp_parallel_copyin_default_none()
// CIR-TLS: omp.parallel {
// CIR-TLS: cir.if
// CIR-TLS: omp.barrier

// CIR-TLS-LABEL: cir.func{{.*}} @test_omp_parallel_copyin_static_local()
// CIR-TLS: %[[MASTER:.*]] = cir.get_global thread_local @test_omp_parallel_copyin_static_local.s : !cir.ptr<!s32i>
// CIR-TLS: omp.parallel {
// CIR-TLS: %[[PRIVATE_COPYIN:.*]] = cir.get_global thread_local @test_omp_parallel_copyin_static_local.s : !cir.ptr<!s32i>
// CIR-TLS: %[[NE:.*]] = cir.cmp(ne, %[[MASTER]], %[[PRIVATE_COPYIN]]) : !cir.ptr<!s32i>, !cir.bool
// CIR-TLS: cir.if %[[NE]] {
// CIR-TLS: %[[VAL:.*]] = cir.load align(4) %[[MASTER]] : !cir.ptr<!s32i>, !s32i
// CIR-TLS: cir.store align(4) %[[VAL]], %[[PRIVATE_COPYIN]] : !s32i, !cir.ptr<!s32i>
// CIR-TLS: }
// CIR-TLS: omp.barrier
// CIR-TLS: %[[PRIVATE_BODY:.*]] = cir.get_global thread_local @test_omp_parallel_copyin_static_local.s : !cir.ptr<!s32i>
// CIR-TLS: %[[CUR:.*]] = cir.load align(4) %[[PRIVATE_BODY]] : !cir.ptr<!s32i>, !s32i
// CIR-TLS: %[[INC:.*]] = cir.unary(inc, %[[CUR]]) nsw : !s32i, !s32i
// CIR-TLS: cir.store align(4) %[[INC]], %[[PRIVATE_BODY]] : !s32i, !cir.ptr<!s32i>

// CIR-OMP-LABEL: cir.func{{.*}} @test_omp_parallel_copyin(
// CIR-OMP: %[[BASE:.*]] = cir.get_global @x : !cir.ptr<!s32i>
// CIR-OMP: %[[TPINIT:.*]] = omp.threadprivate %[[BASE]] : !cir.ptr<!s32i> -> !cir.ptr<!s32i>
// CIR-OMP: cir.store{{.*}} %{{.*}}, %[[TPINIT]] : !s32i, !cir.ptr<!s32i>
// CIR-OMP: %[[MASTER:.*]] = cir.get_global @x : !cir.ptr<!s32i>
// CIR-OMP: omp.parallel {
// CIR-OMP: %[[BASE2:.*]] = cir.get_global @x : !cir.ptr<!s32i>
// CIR-OMP: %[[PRIVATE:.*]] = omp.threadprivate %[[BASE2]] : !cir.ptr<!s32i> -> !cir.ptr<!s32i>
// CIR-OMP: %[[NE:.*]] = cir.cmp(ne, %[[MASTER]], %[[PRIVATE]]) : !cir.ptr<!s32i>, !cir.bool
// CIR-OMP: cir.if %[[NE]] {
// CIR-OMP: %[[VAL:.*]] = cir.load align(4) %[[MASTER]] : !cir.ptr<!s32i>, !s32i
// CIR-OMP: cir.store align(4) %[[VAL]], %[[PRIVATE]] : !s32i, !cir.ptr<!s32i>
// CIR-OMP: }
// CIR-OMP: omp.barrier

// LLVM-TLS-LABEL: define internal void @test_omp_parallel_copyin..omp_par(
// LLVM-TLS: %[[MASTER:.*]] = load ptr, ptr %gep_, align 8
// LLVM-TLS: %[[PRIVATE:.*]] = call ptr @llvm.threadlocal.address.p0(ptr @x)
// LLVM-TLS: %[[NE:.*]] = icmp ne ptr %[[MASTER]], %[[PRIVATE]]
// LLVM-TLS: br i1 %[[NE]], label %[[COPY:.*]], label %[[AFTER:.*]]
// LLVM-TLS: [[AFTER]]:
// LLVM-TLS: call void @__kmpc_barrier(
// LLVM-TLS: [[COPY]]:
// LLVM-TLS: %[[VAL:.*]] = load i32, ptr %[[MASTER]], align 4
// LLVM-TLS: store i32 %[[VAL]], ptr %[[PRIVATE]], align 4

// LLVM-TLS-LABEL: define internal void @test_omp_parallel_for_copyin..omp_par(
// LLVM-TLS: %[[PRIVATE:.*]] = call ptr @llvm.threadlocal.address.p0(ptr @x)
// LLVM-TLS: %[[NE:.*]] = icmp ne ptr %loadgep_, %[[PRIVATE]]
// LLVM-TLS: br i1 %[[NE]], label %[[COPY:.*]], label %[[AFTER:.*]]
// LLVM-TLS: [[AFTER]]:
// LLVM-TLS: call void @__kmpc_barrier(
// LLVM-TLS: call void @__kmpc_for_static_init_4{{u?}}(

// LLVM-TLS-LABEL: define internal void @test_omp_parallel_for_simd_copyin..omp_par(
// LLVM-TLS: %[[PRIVATE:.*]] = call ptr @llvm.threadlocal.address.p0(ptr @x)
// LLVM-TLS: %[[NE:.*]] = icmp ne ptr %loadgep_, %[[PRIVATE]]
// LLVM-TLS: br i1 %[[NE]], label %[[COPY:.*]], label %[[AFTER:.*]]
// LLVM-TLS: [[AFTER]]:
// LLVM-TLS: call void @__kmpc_barrier(
// LLVM-TLS: call void @__kmpc_for_static_init_4{{u?}}(

// LLVM-TLS-LABEL: define internal void @test_omp_parallel_sections_copyin..omp_par(
// LLVM-TLS: %[[PRIVATE:.*]] = call ptr @llvm.threadlocal.address.p0(ptr @x)
// LLVM-TLS: %[[NE:.*]] = icmp ne ptr %loadgep_, %[[PRIVATE]]
// LLVM-TLS: br i1 %[[NE]], label %[[COPY:.*]], label %[[AFTER:.*]]
// LLVM-TLS: [[AFTER]]:
// LLVM-TLS: call void @__kmpc_barrier(
// LLVM-TLS: call void @__kmpc_for_static_init_4{{u?}}(

// LLVM-TLS-LABEL: define internal void @test_omp_parallel_master_copyin..omp_par(
// LLVM-TLS: %[[PRIVATE:.*]] = call ptr @llvm.threadlocal.address.p0(ptr @x)
// LLVM-TLS: %[[NE:.*]] = icmp ne ptr %loadgep_, %[[PRIVATE]]
// LLVM-TLS: br i1 %[[NE]], label %[[COPY:.*]], label %[[AFTER:.*]]
// LLVM-TLS: [[AFTER]]:
// LLVM-TLS: call void @__kmpc_barrier(
// LLVM-TLS: call i32 @__kmpc_master(

// LLVM-OMP-LABEL: define internal void @test_omp_parallel_copyin..omp_par(
// LLVM-OMP: %[[PRIVATE:.*]] = call ptr @__kmpc_threadprivate_cached(ptr @1, i32 %{{.*}}, ptr @x, i64 4, ptr @x.cache)
// LLVM-OMP: %[[NE:.*]] = icmp ne ptr @x, %[[PRIVATE]]
// LLVM-OMP: br i1 %[[NE]], label %[[COPY:.*]], label %[[AFTER:.*]]
// LLVM-OMP: [[AFTER]]:
// LLVM-OMP: call void @__kmpc_barrier(
// LLVM-OMP: [[COPY]]:
// LLVM-OMP: %[[VAL:.*]] = load i32, ptr @x, align 4
// LLVM-OMP: store i32 %[[VAL]], ptr %[[PRIVATE]], align 4

// LLVM-TLS-LABEL: define dso_local void @test_omp_parallel_copyin_default_none(
// LLVM-TLS-LABEL: define dso_local void @test_omp_parallel_copyin_static_local(
// LLVM-TLS-LABEL: define internal void @test_omp_parallel_copyin_static_local..omp_par(
// LLVM-TLS: %[[MASTER:.*]] = load ptr, ptr %gep_, align 8
// LLVM-TLS: %[[PRIVATE_COPYIN:.*]] = call ptr @llvm.threadlocal.address.p0(ptr @test_omp_parallel_copyin_static_local.s)
// LLVM-TLS: %[[NE:.*]] = icmp ne ptr %[[MASTER]], %[[PRIVATE_COPYIN]]
// LLVM-TLS: br i1 %[[NE]], label %[[COPY:.*]], label %[[AFTER:.*]]
// LLVM-TLS: [[AFTER]]:
// LLVM-TLS: call void @__kmpc_barrier(
// LLVM-TLS: %[[PRIVATE_BODY:.*]] = call ptr @llvm.threadlocal.address.p0(ptr @test_omp_parallel_copyin_static_local.s)
// LLVM-TLS: %[[CUR:.*]] = load i32, ptr %[[PRIVATE_BODY]], align 4
// LLVM-TLS: %[[INC:.*]] = add nsw i32 %[[CUR]], 1
// LLVM-TLS: store i32 %[[INC]], ptr %[[PRIVATE_BODY]], align 4
// LLVM-TLS: [[COPY]]:
// LLVM-TLS: %[[VAL:.*]] = load i32, ptr %[[MASTER]], align 4
// LLVM-TLS: store i32 %[[VAL]], ptr %[[PRIVATE_COPYIN]], align 4
// LLVM-TLS: br label %[[AFTER]]
// LLVM-OMP-LABEL: define dso_local void @test_omp_parallel_copyin_default_none(

// OGCG-TLS-LABEL: define internal void @test_omp_parallel_copyin.omp_outlined(
// OGCG-TLS: call {{.*}}ptr @llvm.threadlocal.address.p0(ptr {{.*}}@x)
// OGCG-TLS: icmp ne i64
// OGCG-TLS: br i1 %{{.*}}, label %copyin.not.master, label %copyin.not.master.end
// OGCG-TLS: copyin.not.master:
// OGCG-TLS: load i32, ptr %{{.*}}, align 4
// OGCG-TLS: store i32 %{{.*}}, ptr %{{.*}}, align 4
// OGCG-TLS: copyin.not.master.end:
// OGCG-TLS: call void @__kmpc_barrier(

// OGCG-TLS-LABEL: define internal void @test_omp_parallel_for_copyin.omp_outlined(
// OGCG-TLS: call {{.*}}ptr @llvm.threadlocal.address.p0(ptr {{.*}}@x)
// OGCG-TLS: icmp ne i64
// OGCG-TLS: br i1 %{{.*}}, label %copyin.not.master, label %copyin.not.master.end
// OGCG-TLS: copyin.not.master.end:
// OGCG-TLS: call void @__kmpc_barrier(
// OGCG-TLS: call void @__kmpc_for_static_init_4{{u?}}(

// OGCG-TLS-LABEL: define dso_local void @test_omp_parallel_copyin_default_none(
// OGCG-TLS-LABEL: define dso_local void @test_omp_parallel_copyin_static_local(
// OGCG-TLS-LABEL: define internal void @test_omp_parallel_copyin_static_local.omp_outlined(
// OGCG-TLS: call {{.*}}ptr @llvm.threadlocal.address.p0(ptr {{.*}}@test_omp_parallel_copyin_static_local.s)
// OGCG-TLS: icmp ne i64
// OGCG-TLS: br i1 %{{.*}}, label %copyin.not.master, label %copyin.not.master.end
// OGCG-TLS: copyin.not.master.end:
// OGCG-TLS: call void @__kmpc_barrier(

// OGCG-OMP-LABEL: define internal void @test_omp_parallel_copyin.omp_outlined(
// OGCG-OMP: call ptr @__kmpc_threadprivate_cached(ptr @{{.*}}, i32 %{{.*}}, ptr @x, i64 4, ptr @x.cache.)
// OGCG-OMP: icmp ne i64
// OGCG-OMP: br i1 %{{.*}}, label %copyin.not.master, label %copyin.not.master.end
// OGCG-OMP: copyin.not.master:
// OGCG-OMP: load i32, ptr @x, align 4
// OGCG-OMP: store i32 %{{.*}}, ptr %{{.*}}, align 4
// OGCG-OMP: copyin.not.master.end:
// OGCG-OMP: call void @__kmpc_barrier(

// OGCG-OMP-LABEL: define dso_local void @test_omp_parallel_copyin_default_none(
