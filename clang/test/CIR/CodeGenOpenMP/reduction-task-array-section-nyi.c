// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir \
// RUN:   -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

void task_array_section_reduction(int argc, char **argv) {
  // CIR-DAG: cir.global "private"  internal  @[[SIZE_SYM:__omp_task_reduction_size_argv_[0-9]+_[0-9]+]] = #cir.int<0> : !u64i
  // CIR-DAG: omp.declare_reduction @[[RED_SYM:__omp_reduction_add_byref_s8_dynamic_argv_[0-9]+_[0-9]+]] : !cir.ptr<!s8i> attributes {byref_element_type = !s8i, omp.frontend.task_reduction_size_sym = @[[SIZE_SYM]]} alloc {
  // CIR: %[[SIZE_GLOBAL:.*]] = cir.get_global @[[SIZE_SYM]] : !cir.ptr<!u64i>
  // CIR: %[[SIZE_TLS:.*]] = omp.threadprivate %[[SIZE_GLOBAL]] : !cir.ptr<!u64i> -> !cir.ptr<!u64i>
  // CIR: %[[COUNT:.*]] = cir.load align(8) %[[SIZE_TLS]] : !cir.ptr<!u64i>, !u64i
  // CIR: %[[PRIVATE:.*]] = cir.alloca !s8i, !cir.ptr<!s8i>, %[[COUNT]] : !u64i, ["argv"]
  // CIR-LABEL: cir.func{{.*}} @task_array_section_reduction(
  // CIR: %[[DIFF:.*]] = cir.ptr_diff %{{.*}}, %{{.*}} : !cir.ptr<!s8i> -> !s64i
  // CIR: %[[SIZE:.*]] = cir.cast integral %[[DIFF]] : !s64i -> !u64i
  // CIR: %[[ONE:.*]] = cir.const #cir.int<1> : !u64i
  // CIR: %[[RUNTIME_COUNT:.*]] = cir.binop(add, %[[SIZE]], %[[ONE]]) : !u64i
  // CIR: omp.wsloop reduction(mod: task, @__omp_reduction_add_s32 %{{.*}} -> %{{.*}}, byref @[[RED_SYM]] %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!s8i>) {
  // CIR: omp.task in_reduction(@__omp_reduction_add_s32 %{{.*}} -> %{{.*}}, byref @[[RED_SYM]] %{{.*}} -> %{{.*}} : !cir.ptr<!s32i>, !cir.ptr<!s8i>)
#pragma omp parallel
#pragma omp for reduction(task, +: argc, argv[0:10][0:argc])
  for (long long i = 0; i < 10; ++i) {
#pragma omp task in_reduction(+: argc, argv[0:10][0:argc])
    ;
  }
}

// LLVM-DAG: @[[SIZE_SYM:__omp_task_reduction_size_argv_[0-9]+_[0-9]+]] = internal global i64 0
// LLVM-DAG: @[[SIZE_SYM]].cache = common global ptr null, align 8
// LLVM-LABEL: define {{.*}} void @task_array_section_reduction(
// The element count is stored before the private copy is allocated with it.
// LLVM: %[[SET_TLS:.*]] = call ptr @__kmpc_threadprivate_cached(ptr @{{.*}}, i32 %{{.*}}, ptr @[[SIZE_SYM]], i64 8, ptr @[[SIZE_SYM]].cache)
// LLVM-NEXT: store i64 %{{.*}}, ptr %[[SET_TLS]], align 8
// LLVM: %[[COUNT:.*]] = load i64, ptr %{{.*}}, align 8
// LLVM-NEXT: %[[PRIVATE:.*]] = alloca i8, i64 %[[COUNT]], align 16
// LLVM: %[[RD_INPUT1:.rd_input.gep[0-9]+]] = getelementptr inbounds [2 x %struct.kmp_taskred_input_t], ptr %.rd_input., i64 0, i64 1
// LLVM: %[[SIZE_FIELD:.*]] = getelementptr inbounds nuw %struct.kmp_taskred_input_t, ptr %[[RD_INPUT1]], i32 0, i32 2
// LLVM: %[[RED_SIZE:.*]] = mul nuw i64 %[[COUNT]], 1
// LLVM: store i64 %[[RED_SIZE]], ptr %[[SIZE_FIELD]], align 8
// LLVM: %[[FLAG_FIELD:.*]] = getelementptr inbounds nuw %struct.kmp_taskred_input_t, ptr %[[RD_INPUT1]], i32 0, i32 6
// LLVM: store i32 1, ptr %[[FLAG_FIELD]], align 4
// LLVM: %[[TASKRED_DESC:.*]] = call ptr @__kmpc_taskred_modifier_init(ptr @{{.*}}, i32 %{{.*}}, i32 1, i32 2, ptr %.rd_input.)
// LLVM-LABEL: define internal void @task_array_section_reduction..omp_par(
// LLVM: %loadgep_omp.task.context_ptr = load ptr, ptr %gep_omp.task.context_ptr, align 8, !align !{{.*}}
// LLVM: %[[TASK_DESC:.*]] = load ptr, ptr %gep_, align 8, !align !{{.*}}
// LLVM: %[[TASK_SHARED0:.*]] = load ptr, ptr %gep_1, align 8, !align !{{.*}}
// LLVM: %[[TASK_COUNT:.*]] = load i64, ptr %gep_omp.task.reduction.size, align 8
// LLVM: %[[TASK_SHARED1:.*]] = load ptr, ptr %gep_3, align 8, !align !{{.*}}
// LLVM: task.body:
// The task looks up its per-thread private reduction copy with a thread id
// computed inside the task body, not one captured from the creating thread.
// LLVM: %[[TID0:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// LLVM: %{{.*}} = call ptr @__kmpc_task_reduction_get_th_data(i32 %[[TID0]], ptr %[[TASK_DESC]], ptr %[[TASK_SHARED0]])
// LLVM: %[[TASK_TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// LLVM: %[[TASK_SIZE_TLS:.*]] = call ptr @__kmpc_threadprivate_cached(ptr @{{.*}}, i32 %[[TASK_TID]], ptr @[[SIZE_SYM]], i64 8, ptr @[[SIZE_SYM]].cache)
// LLVM: store i64 %[[TASK_COUNT]], ptr %[[TASK_SIZE_TLS]], align 8
// LLVM: %[[TID1:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// LLVM: %{{.*}} = call ptr @__kmpc_task_reduction_get_th_data(i32 %[[TID1]], ptr %[[TASK_DESC]], ptr %[[TASK_SHARED1]])

// OGCG-DAG: @{{.*}}reduction_size.argv{{.*}}artificial{{.*}} = common thread_local global i64 0
// OGCG-LABEL: define dso_local void @task_array_section_reduction(
// OGCG-LABEL: define internal void @task_array_section_reduction.omp_outlined(
// OGCG: call ptr @__kmpc_taskred_modifier_init(ptr @{{.*}}, i32 %{{.*}}, i32 1, i32 2, ptr %.rd_input.)
// OGCG: call void @__kmpc_task_reduction_modifier_fini(ptr @{{.*}}, i32 %{{.*}}, i32 1)
// OGCG: call ptr @__kmpc_task_reduction_get_th_data(i32 %{{.*}}, ptr %{{.*}}, ptr %{{.*}})
