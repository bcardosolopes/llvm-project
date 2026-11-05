// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

typedef enum omp_allocator_handle_t {
  omp_null_allocator = 0,
  omp_default_mem_alloc = 1,
  omp_large_cap_mem_alloc = 2,
  omp_const_mem_alloc = 3,
  omp_high_bw_mem_alloc = 4,
  omp_low_lat_mem_alloc = 5,
  omp_cgroup_mem_alloc = 6,
  omp_pteam_mem_alloc = 7,
  omp_thread_mem_alloc = 8,
  KMP_ALLOCATOR_MAX_HANDLE = __UINTPTR_MAX__
} omp_allocator_handle_t;

void parallel_loop_allocate_private(void) {
  int x = 0;
#pragma omp parallel loop allocate(x) private(x)
  for (int i = 0; i < 4; ++i)
    x += i;
}

// CHECK-LABEL: define{{.*}} @parallel_loop_allocate_private(
// CHECK:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// CHECK-LABEL: define internal void @parallel_loop_allocate_private..omp_par(
// CHECK:         %[[ALLOC_TID:.*]] = call i32 @__kmpc_global_thread_num(
// CHECK:         %[[PRIVATE_ALLOC:.*]] = call ptr @__kmpc_alloc(i32 %[[ALLOC_TID]], i64 4, ptr null)
// CHECK:         call void @__kmpc_for_static_init_4(
// CHECK-DAG:     %{{.*}} = load i32, ptr %[[PRIVATE_ALLOC]], align 4
// CHECK-DAG:     call void @__kmpc_for_static_fini(
// CHECK-DAG:     call void @__kmpc_free(i32 %{{.*}}, ptr %[[PRIVATE_ALLOC]], ptr null)

void parallel_loop_allocate_private_aligned(void) {
  int x = 0;
#pragma omp parallel loop allocate(align(8), allocator(omp_thread_mem_alloc): x) private(x)
  for (int i = 0; i < 4; ++i)
    x += i;
}

// CHECK-LABEL: define{{.*}} @parallel_loop_allocate_private_aligned(
// CHECK:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// CHECK-LABEL: define internal void @parallel_loop_allocate_private_aligned..omp_par(
// CHECK:         %[[ALLOC_TID2:.*]] = call i32 @__kmpc_global_thread_num(
// CHECK:         %[[PRIVATE_ALLOC2:.*]] = call noalias ptr @__kmpc_aligned_alloc(i32 %[[ALLOC_TID2]], i64 8, i64 4, ptr inttoptr (i64 8 to ptr))
// CHECK:         call void @__kmpc_for_static_init_4(
// CHECK:         call void @__kmpc_for_static_fini(
// CHECK:         call void @__kmpc_free(i32 %{{.*}}, ptr %[[PRIVATE_ALLOC2]], ptr inttoptr (i64 8 to ptr))
// CHECK:         %{{.*}} = load i32, ptr %[[PRIVATE_ALLOC2]], align 4
// CHECK:         store i32 %{{.*}}, ptr %[[PRIVATE_ALLOC2]], align 4

void parallel_loop_allocate_firstprivate_aligned(int x) {
#pragma omp parallel loop allocate(align(8), allocator(omp_thread_mem_alloc): x) firstprivate(x)
  for (int i = 0; i < 4; ++i)
    x += i;
}

// CHECK-LABEL: define{{.*}} @parallel_loop_allocate_firstprivate_aligned(
// CHECK-SAME: i32 noundef %[[ARG_IN:[0-9]+]]
// CHECK-LABEL: define internal void @parallel_loop_allocate_firstprivate_aligned..omp_par(
// CHECK:         %[[SRC_PTR:.*]] = load ptr, ptr %{{.*}}, align 8
// CHECK:         %[[ALLOC_TID3:.*]] = call i32 @__kmpc_global_thread_num(
// CHECK:         %[[PRIVATE_ALLOC3:.*]] = call noalias ptr @__kmpc_aligned_alloc(i32 %[[ALLOC_TID3]], i64 8, i64 4, ptr inttoptr (i64 8 to ptr))
// CHECK:         %[[SRC_VAL:.*]] = load i32, ptr %[[SRC_PTR]], align 4
// CHECK:         store i32 %[[SRC_VAL]], ptr %[[PRIVATE_ALLOC3]], align 4
// CHECK:         call void @__kmpc_for_static_init_4(
// CHECK:         call void @__kmpc_for_static_fini(
// CHECK:         call void @__kmpc_free(i32 %{{.*}}, ptr %[[PRIVATE_ALLOC3]], ptr inttoptr (i64 8 to ptr))
// CHECK:         %{{.*}} = load i32, ptr %[[PRIVATE_ALLOC3]], align 4
// CHECK:         store i32 %{{.*}}, ptr %[[PRIVATE_ALLOC3]], align 4

// OGCG-LABEL: define{{.*}} @parallel_loop_allocate_private(
// OGCG:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG:         call ptr @__kmpc_alloc(
// OGCG:         call void @__kmpc_for_static_init_4(
// OGCG:         call void @__kmpc_for_static_fini(
// OGCG:         call void @__kmpc_free(

// OGCG-LABEL: define{{.*}} @parallel_loop_allocate_private_aligned(
// OGCG:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG:         call {{.*}}ptr @__kmpc_aligned_alloc(
// OGCG:         call void @__kmpc_for_static_init_4(
// OGCG:         call void @__kmpc_for_static_fini(
// OGCG:         call void @__kmpc_free(

// OGCG-LABEL: define{{.*}} @parallel_loop_allocate_firstprivate_aligned(
// OGCG:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// OGCG:         call {{.*}}ptr @__kmpc_aligned_alloc(
// OGCG:         call void @__kmpc_for_static_init_4(
// OGCG:         call void @__kmpc_for_static_fini(
// OGCG:         call void @__kmpc_free(
