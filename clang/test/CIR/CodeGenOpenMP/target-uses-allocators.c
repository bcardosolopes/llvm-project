// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir -I %S/../../Headers/Inputs/include %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm -I %S/../../Headers/Inputs/include %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fopenmp-version=50 -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm -I %S/../../Headers/Inputs/include %s -o - | FileCheck %s --check-prefix=OGCG

#include <omp.h>

typedef enum omp_alloctrait_key_t {
  omp_atk_alignment = 2,
} omp_alloctrait_key_t;

typedef struct omp_alloctrait_t {
  omp_alloctrait_key_t key;
  __UINTPTR_TYPE__ value;
} omp_alloctrait_t;

void target_allocate_firstprivate(int x) {
#pragma omp target uses_allocators(omp_default_mem_alloc) allocate(omp_default_mem_alloc : x) firstprivate(x)
  {
  }
}

// CHECK-LABEL: define dso_local void @target_allocate_firstprivate(i32 noundef %0)
// CHECK:         call void @__omp_offloading{{.*}}target_allocate_firstprivate{{.*}}(i64 %{{.*}})

void target_uses_allocator_traits(void) {
  omp_allocator_handle_t my_allocator;
  omp_alloctrait_t traits[1] = {{omp_atk_alignment, 64}};
#pragma omp target uses_allocators(my_allocator(traits))
  {
  }
}

void target_uses_allocator_traits_allocate_firstprivate(int x) {
  omp_allocator_handle_t my_allocator;
  omp_alloctrait_t traits[1] = {{omp_atk_alignment, 64}};
#pragma omp target uses_allocators(my_allocator(traits)) allocate(my_allocator : x) firstprivate(x)
  {
  }
}

// CIR-LABEL: cir.func no_inline optnone dso_local @target_uses_allocator_traits_allocate_firstprivate(
// CIR:         %[[TRAITS_MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!cir.array<!rec_omp_alloctrait_t x 1>>, !cir.array<!rec_omp_alloctrait_t x 1>) map_clauses(implicit) capture(ByRef) -> !cir.ptr<!cir.array<!rec_omp_alloctrait_t x 1>> {name = "traits"}
// CIR:         %[[X_MAP:.*]] = omp.map.info var_ptr(%{{.*}} : !cir.ptr<!s32i>, !s32i) map_clauses(none) capture(ByCopy) -> !cir.ptr<!s32i>
// CIR:         omp.target map_entries(%[[X_MAP]] -> %{{.*}}, %[[TRAITS_MAP]] -> %{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!cir.array<!rec_omp_alloctrait_t x 1>>) private(@{{.*}} %{{.*}} -> %{{.*}} [map_idx=0] : !cir.ptr<!s32i>) {

// CHECK-LABEL: define dso_local void @target_uses_allocator_traits_allocate_firstprivate(i32 noundef %0)
// CHECK:         call void @__omp_offloading{{.*}}target_uses_allocator_traits_allocate_firstprivate{{.*}}(i64 %{{.*}}, ptr %{{.*}})
// CHECK-LABEL: define internal void @__omp_offloading{{.*}}target_allocate_firstprivate{{.*}}(i64 noundef %0)
// CHECK:         %[[ALLOC_TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// CHECK:         %[[PRIVATE_ALLOC:.*]] = call ptr @__kmpc_alloc(i32 %[[ALLOC_TID]], i64 4, ptr inttoptr (i32 1 to ptr))
// CHECK:         %[[XVAL:.*]] = load i32, ptr %{{[^,]+}}, align 4
// CHECK:         store i32 %[[XVAL]], ptr %[[PRIVATE_ALLOC]], align 4
// CHECK:         %[[FREE_TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// CHECK:         call void @__kmpc_free(i32 %[[FREE_TID]], ptr %[[PRIVATE_ALLOC]], ptr inttoptr (i32 1 to ptr))
// CHECK-LABEL: define internal void @__omp_offloading{{.*}}target_uses_allocator_traits{{.*}}(ptr noundef nonnull align 8 dereferenceable(16) %0)
// CHECK:         %[[ALLOC_STORAGE:.*]] = alloca i32, i64 1, align 4
// CHECK:         %[[INIT_TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// CHECK:         %[[ALLOCATOR:.*]] = call ptr @__kmpc_init_allocator(i32 %[[INIT_TID]], ptr null, i32 1, ptr %0)
// CHECK:         %[[ALLOCATOR_I32:.*]] = ptrtoint ptr %[[ALLOCATOR]] to i32
// CHECK:         store i32 %[[ALLOCATOR_I32]], ptr %[[ALLOC_STORAGE]], align 4
// CHECK:         %[[LOADED_ALLOCATOR:.*]] = load i32, ptr %[[ALLOC_STORAGE]], align 4
// CHECK:         %[[ALLOCATOR_PTR:.*]] = inttoptr i32 %[[LOADED_ALLOCATOR]] to ptr
// CHECK:         %[[DESTROY_TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// CHECK:         call void @__kmpc_destroy_allocator(i32 %[[DESTROY_TID]], ptr %[[ALLOCATOR_PTR]])
// CHECK-LABEL: define internal void @__omp_offloading{{.*}}target_uses_allocator_traits_allocate_firstprivate{{.*}}(i64 noundef %0, ptr noundef nonnull align 8 dereferenceable(16) %1)
// CHECK:         %[[ALLOC_STORAGE:.*]] = alloca i32, i64 1, align 4
// CHECK:         %[[INIT_TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// CHECK:         %[[ALLOCATOR:.*]] = call ptr @__kmpc_init_allocator(i32 %[[INIT_TID]], ptr null, i32 1, ptr %1)
// CHECK:         %[[ALLOCATOR_I32:.*]] = ptrtoint ptr %[[ALLOCATOR]] to i32
// CHECK:         store i32 %[[ALLOCATOR_I32]], ptr %[[ALLOC_STORAGE]], align 4
// CHECK:         %[[PRIVATE_ALLOCATOR_I32:.*]] = load i32, ptr %[[ALLOC_STORAGE]], align 4
// CHECK:         %[[PRIVATE_ALLOCATOR:.*]] = inttoptr i32 %[[PRIVATE_ALLOCATOR_I32]] to ptr
// CHECK:         %[[PRIVATE_ALLOC:.*]] = call noalias ptr @__kmpc_alloc(i32 %{{.*}}, i64 4, ptr %[[PRIVATE_ALLOCATOR]])
// CHECK:         %[[XVAL:.*]] = load i32, ptr %{{[^,]+}}, align 4
// CHECK:         store i32 %[[XVAL]], ptr %[[PRIVATE_ALLOC]], align 4
// CHECK:         %[[FREE_ALLOCATOR_I32:.*]] = load i32, ptr %[[ALLOC_STORAGE]], align 4
// CHECK:         %[[FREE_ALLOCATOR:.*]] = inttoptr i32 %[[FREE_ALLOCATOR_I32]] to ptr
// CHECK:         call void @__kmpc_free(i32 %{{.*}}, ptr %[[PRIVATE_ALLOC]], ptr %[[FREE_ALLOCATOR]])
// CHECK:         %[[DESTROY_ALLOCATOR_I32:.*]] = load i32, ptr %[[ALLOC_STORAGE]], align 4
// CHECK:         %[[DESTROY_ALLOCATOR_PTR:.*]] = inttoptr i32 %[[DESTROY_ALLOCATOR_I32]] to ptr
// CHECK:         call void @__kmpc_destroy_allocator(i32 %{{.*}}, ptr %[[DESTROY_ALLOCATOR_PTR]])

// OGCG-LABEL: define dso_local void @target_allocate_firstprivate(
// OGCG:         define internal void @__omp_offloading_{{.*}}target_allocate_firstprivate
// OGCG:         call ptr @__kmpc_alloc(
// OGCG:         call void @__kmpc_free(

// OGCG-LABEL: define dso_local void @target_uses_allocator_traits(
// OGCG:         define internal void @__omp_offloading_{{.*}}target_uses_allocator_traits
// OGCG:         call ptr @__kmpc_init_allocator(
// OGCG:         call void @__kmpc_destroy_allocator(

// OGCG-LABEL: define dso_local void @target_uses_allocator_traits_allocate_firstprivate(
// OGCG:         define internal void @__omp_offloading_{{.*}}target_uses_allocator_traits_allocate_firstprivate
// OGCG:         call ptr @__kmpc_init_allocator(
// OGCG:         call {{.*}}ptr @__kmpc_alloc(
// OGCG:         call void @__kmpc_free(
// OGCG:         call void @__kmpc_destroy_allocator(
