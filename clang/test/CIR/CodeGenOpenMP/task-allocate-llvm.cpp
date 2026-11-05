// RUN: %clang_cc1 -x c++ -fopenmp -I %S/../../Headers/Inputs/include -emit-llvm -fclangir %s -o - | FileCheck %s

void body();

#include <omp.h>

void task_allocate_simple(int x) {
#pragma omp task firstprivate(x) allocate(omp_pteam_mem_alloc:x)
  {
    body();
  }
}

// CHECK-LABEL: define{{.*}} @_Z20task_allocate_simplei(
// CHECK:         %[[ALLOC_TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.+}})
// CHECK:         %[[PRIVATE_ALLOC:.*]] = call ptr @__kmpc_alloc(i32 %[[ALLOC_TID]], i64 4, ptr inttoptr (i32 7 to ptr))
// CHECK:         %[[TASK_DATA:.*]] = call ptr @__kmpc_omp_task_alloc
// CHECK:         call i32 @__kmpc_omp_task(ptr @{{.+}}, i32 %{{.+}}, ptr %[[TASK_DATA]])
//
// CHECK-LABEL: define internal void @{{.*}}omp_par
// CHECK:         %[[SHAREDS:.*]] = load ptr, ptr %[[STRUCT_ARG:.*]], align 8
// CHECK:         %[[CAPTURE_GEP:.*]] = getelementptr { ptr }, ptr %[[SHAREDS]], i32 0, i32 0
// CHECK:         %[[ALLOCATED_PTR:.*]] = load ptr, ptr %[[CAPTURE_GEP]], align 8
// CHECK:         call void @_Z4bodyv()
// CHECK:         %[[FREE_TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.+}})
// CHECK:         call void @__kmpc_free(i32 %[[FREE_TID]], ptr %[[ALLOCATED_PTR]], ptr inttoptr (i32 7 to ptr))
