// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir \
// RUN:   -emit-cir -Wno-vla %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir \
// RUN:   -emit-llvm -Wno-vla %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm -Wno-vla %s -o - | FileCheck %s --check-prefix=OGCG

void taskgroup_vla(int n) {
  short d[n];
  // CIR-DAG: cir.global "private"  internal  @[[SIZE_SYM:__omp_task_reduction_size_d_[0-9]+_[0-9]+]] = #cir.int<0> : !u64i
  // CIR-DAG: omp.declare_reduction @[[RED_SYM:__omp_reduction_sub_byref_s16_dynamic_d_[0-9]+_[0-9]+]] : !cir.ptr<!s16i> attributes {byref_element_type = !s16i, omp.frontend.task_reduction_size_sym = @[[SIZE_SYM]]}
  // CIR-LABEL: cir.func{{.*}} @taskgroup_vla(
  // CIR: omp.taskgroup task_reduction(byref @[[RED_SYM]] %{{.*}} -> %[[TGARG:arg[0-9]+]] : !cir.ptr<!s16i>) {
  // CIR: %[[SIZE_GLOBAL:.*]] = cir.get_global @[[SIZE_SYM]] : !cir.ptr<!u64i>
  // CIR: %[[SIZE_TLS:.*]] = omp.threadprivate %[[SIZE_GLOBAL]] : !cir.ptr<!u64i> -> !cir.ptr<!u64i>
  // CIR: cir.store align(8) %[[COUNT:.*]], %[[SIZE_TLS]] : !u64i, !cir.ptr<!u64i>
  // CIR: omp.task in_reduction(byref @[[RED_SYM]] %[[TGARG]] -> %[[TASKARG:arg[0-9]+]] : !cir.ptr<!s16i>)
  // CIR: %[[INNER_SIZE_GLOBAL:.*]] = cir.get_global @[[SIZE_SYM]] : !cir.ptr<!u64i>
  // CIR: %[[INNER_SIZE_TLS:.*]] = omp.threadprivate %[[INNER_SIZE_GLOBAL]] : !cir.ptr<!u64i> -> !cir.ptr<!u64i>
  // CIR: cir.store align(8) %[[COUNT]], %[[INNER_SIZE_TLS]] : !u64i, !cir.ptr<!u64i>
#pragma omp taskgroup task_reduction(-: d)
  {
#pragma omp task in_reduction(-: d)
    {}
  }
}

// LLVM-DAG: @[[SIZE_SYM:__omp_task_reduction_size_d_[0-9]+_[0-9]+]] = internal global i64 0
// LLVM-DAG: @[[SIZE_SYM]].cache = common global ptr null, align 8
// LLVM-LABEL: define {{.*}} void @taskgroup_vla(
// LLVM: %.rd_input.gep = getelementptr inbounds [1 x %struct.kmp_taskred_input_t], ptr %.rd_input., i64 0, i64 0
// LLVM: %[[SIZE_FIELD:.*]] = getelementptr inbounds nuw %struct.kmp_taskred_input_t, ptr %.rd_input.gep, i32 0, i32 2
// LLVM: %[[RED_SIZE:.*]] = mul nuw i64 %[[COUNT:[0-9]+]], 2
// LLVM: store i64 %[[RED_SIZE]], ptr %[[SIZE_FIELD]], align 8
// LLVM: call ptr @__kmpc_taskred_init(i32 %{{.*}}, i32 1, ptr %.rd_input.)
// LLVM: %[[SIZE_TLS:.*]] = call ptr @__kmpc_threadprivate_cached(ptr @{{.*}}, i32 %{{.*}}, ptr @[[SIZE_SYM]], i64 8, ptr @[[SIZE_SYM]].cache)
// LLVM: store i64 %[[COUNT]], ptr %[[SIZE_TLS]], align 8
// LLVM: %[[TASK_SIZE_TLS:.*]] = call ptr @__kmpc_threadprivate_cached(ptr @{{.*}}, i32 %{{.*}}, ptr @[[SIZE_SYM]], i64 8, ptr @[[SIZE_SYM]].cache)
// LLVM: store i64 %{{.*}}, ptr %[[TASK_SIZE_TLS]], align 8

// OGCG-DAG: @{{.*}}reduction_size.d{{.*}}artificial{{.*}} = common thread_local global i64 0
// OGCG-LABEL: define dso_local void @taskgroup_vla(
// OGCG: call ptr @__kmpc_taskred_init(i32 %{{.*}}, i32 1, ptr %.rd_input.)
// OGCG: call {{.*}}ptr @llvm.threadlocal.address.p0(ptr {{.*}}@{{.*}}reduction_size.d{{.*}}artificial{{.*}})
