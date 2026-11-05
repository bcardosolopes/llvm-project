// RUN: %clang_cc1 -fopenmp -fopenmp-version=52 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -fopenmp-version=52 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

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

void scope_private_allocate(void) {
  int v;
#pragma omp scope private(v) allocate(allocator(omp_pteam_mem_alloc): v)
  ++v;
}

// CHECK-LABEL: define{{.*}} @scope_private_allocate(
// CHECK:         %[[ALLOC_TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.+}})
// CHECK:         %[[PRIVATE_ALLOC:.*]] = call noalias ptr @__kmpc_alloc(i32 %[[ALLOC_TID]], i64 4, ptr inttoptr (i64 7 to ptr))
// CHECK:         %[[PRIVATE_VAL:.*]] = load i32, ptr %[[PRIVATE_ALLOC]], align 4
// CHECK:         %[[PRIVATE_INC:.*]] = add nsw i32 %[[PRIVATE_VAL]], 1
// CHECK:         store i32 %[[PRIVATE_INC]], ptr %[[PRIVATE_ALLOC]], align 4
// CHECK:         %[[FREE_TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.+}})
// CHECK:         call void @__kmpc_free(i32 %[[FREE_TID]], ptr %[[PRIVATE_ALLOC]], ptr inttoptr (i64 7 to ptr))
// CHECK:         %[[BARRIER_TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.+}})
// CHECK:         call void @__kmpc_barrier(ptr @{{.+}}, i32 %[[BARRIER_TID]])

void scope_firstprivate_aligned_allocate(int x) {
#pragma omp scope firstprivate(x) allocate(align(8), allocator(omp_thread_mem_alloc): x)
  ++x;
}

// CHECK-LABEL: define{{.*}} @scope_firstprivate_aligned_allocate(
// CHECK-SAME: i32 noundef %[[ARG_IN:[0-9]+]]
// CHECK:         %[[ARG_ADDR:[0-9]+]] = alloca i32
// CHECK:         store i32 %[[ARG_IN]], ptr %[[ARG_ADDR]], align 4
// CHECK:         %[[ALLOC_TID2:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.+}})
// CHECK:         %[[PRIVATE_ALLOC2:.*]] = call noalias ptr @__kmpc_aligned_alloc(i32 %[[ALLOC_TID2]], i64 8, i64 4, ptr inttoptr (i64 8 to ptr))
// CHECK:         %[[ARG_VAL:.*]] = load i32, ptr %[[ARG_ADDR]], align 4
// CHECK:         store i32 %[[ARG_VAL]], ptr %[[PRIVATE_ALLOC2]], align 4
// CHECK:         %[[PRIVATE_VAL2:.*]] = load i32, ptr %[[PRIVATE_ALLOC2]], align 4
// CHECK:         %[[PRIVATE_INC2:.*]] = add nsw i32 %[[PRIVATE_VAL2]], 1
// CHECK:         store i32 %[[PRIVATE_INC2]], ptr %[[PRIVATE_ALLOC2]], align 4
// CHECK:         %[[FREE_TID2:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.+}})
// CHECK:         call void @__kmpc_free(i32 %[[FREE_TID2]], ptr %[[PRIVATE_ALLOC2]], ptr inttoptr (i64 8 to ptr))
// CHECK:         %[[BARRIER_TID2:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.+}})
// CHECK:         call void @__kmpc_barrier(ptr @{{.+}}, i32 %[[BARRIER_TID2]])

void scope_private_allocate_nowait(void) {
  int v;
#pragma omp scope nowait private(v) allocate(v)
  ++v;
}

// CHECK-LABEL: define{{.*}} @scope_private_allocate_nowait(
// CHECK:         %[[ALLOC_TID3:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.+}})
// CHECK:         %[[PRIVATE_ALLOC3:.*]] = call noalias ptr @__kmpc_alloc(i32 %[[ALLOC_TID3]], i64 4, ptr null)
// CHECK:         %[[PRIVATE_VAL3:.*]] = load i32, ptr %[[PRIVATE_ALLOC3]], align 4
// CHECK:         %[[PRIVATE_INC3:.*]] = add nsw i32 %[[PRIVATE_VAL3]], 1
// CHECK:         store i32 %[[PRIVATE_INC3]], ptr %[[PRIVATE_ALLOC3]], align 4
// CHECK:         %[[FREE_TID3:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.+}})
// CHECK:         call void @__kmpc_free(i32 %[[FREE_TID3]], ptr %[[PRIVATE_ALLOC3]], ptr null)
// CHECK-NOT:     call void @__kmpc_barrier
// CHECK:         ret void

// OGCG-LABEL: define{{.*}} @scope_private_allocate(
// OGCG:         call ptr @__kmpc_alloc(
// OGCG:         call void @__kmpc_free(
// OGCG:         call void @__kmpc_barrier(

// OGCG-LABEL: define{{.*}} @scope_firstprivate_aligned_allocate(
// OGCG:         call {{.*}}ptr @__kmpc_aligned_alloc(
// OGCG:         call void @__kmpc_free(
// OGCG:         call void @__kmpc_barrier(

// OGCG-LABEL: define{{.*}} @scope_private_allocate_nowait(
// OGCG:         call ptr @__kmpc_alloc(
// OGCG:         call void @__kmpc_free(
// OGCG-NOT:     call void @__kmpc_barrier
// OGCG:         ret void
