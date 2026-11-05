// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

module attributes {omp.is_target_device = false} {
  llvm.func @omp_target_depend_() {
    %0 = llvm.mlir.constant(39 : index) : i64
    %1 = llvm.mlir.constant(1 : index) : i64
    %2 = llvm.mlir.constant(40 : index) : i64
    %3 = omp.map.bounds lower_bound(%1 : i64) upper_bound(%0 : i64) extent(%2 : i64) stride(%1 : i64) start_idx(%1 : i64)
    %4 = llvm.mlir.addressof @_QFEa : !llvm.ptr
    %5 = omp.map.info var_ptr(%4 : !llvm.ptr, !llvm.array<40 x i32>) map_clauses(from) capture(ByRef) bounds(%3) -> !llvm.ptr {name = "a"}
    omp.target depend(taskdependin -> %4 : !llvm.ptr) map_entries(%5 -> %arg0 : !llvm.ptr) {
      %6 = llvm.mlir.constant(100 : index) : i32
      llvm.store %6, %arg0 : i32, !llvm.ptr
      omp.terminator
    }
    llvm.return
  }

  llvm.mlir.global internal @_QFEa() {addr_space = 0 : i32} : !llvm.array<40 x i32> {
    %0 = llvm.mlir.zero : !llvm.array<40 x i32>
    llvm.return %0 : !llvm.array<40 x i32>
  }
}

// https://github.com/llvm/llvm-project/issues/126949 exposes two issues
// 1. Empty target task proxy functions
// 2. When 1 fixed, it leads to a second problem of calling the omp target kernel twice
//    Once via the target task proxy function and a second time after the target task is done.
// The following checks check problem #2.
// functions. The following checks tests the fix for this issue.




// The following check test for the fix of problem #1 as described in https://github.com/llvm/llvm-project/issues/126949

// CHECK: @_QFEa = internal global [40 x i32] zeroinitializer
// CHECK: @0 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @1 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @1 }, align 8
// CHECK-LABEL: define void @omp_target_depend_(
// CHECK:   %[[VAL_0:.*]] = alloca [1 x %[[VAL_1:.*]]], align 8
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_4:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   %[[VAL_5:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @2, i32 %[[VAL_4]], i32 0, i64 40, i64 0, ptr @.omp_target_task_proxy_func)
// CHECK:   %[[VAL_6:.*]] = getelementptr inbounds [1 x %[[VAL_1]]], ptr %[[VAL_0]], i64 0, i64 0
// CHECK:   %[[VAL_7:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_6]], i32 0, i32 0
// CHECK:   store i64 ptrtoint (ptr @_QFEa to i64), ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_8:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_6]], i32 0, i32 1
// CHECK:   store i64 160, ptr %[[VAL_8]], align 4
// CHECK:   %[[VAL_9:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_6]], i32 0, i32 2
// CHECK:   store i8 1, ptr %[[VAL_9]], align 1
// CHECK:   call void @__kmpc_omp_taskwait_deps_51(ptr @2, i32 %[[VAL_4]], i32 1, ptr %[[VAL_0]], i32 0, ptr null, i32 0)
// CHECK:   call void @__kmpc_omp_task_begin_if0(ptr @2, i32 %[[VAL_4]], ptr %[[VAL_5]])
// CHECK:   call void @.omp_target_task_proxy_func(i32 %[[VAL_4]], ptr %[[VAL_5]])
// CHECK:   call void @__kmpc_omp_task_complete_if0(ptr @2, i32 %[[VAL_4]], ptr %[[VAL_5]])
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: target.task.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_target_depend_..omp_par(
// CHECK: target.task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: target.task.body:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_omp_target_depend__l{{.*}}(ptr @_QFEa)
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: target.task.cont.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_omp_target_depend__l{{.*}}(
// CHECK: entry:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.target:
// CHECK:   store i32 100, ptr %[[VAL_3:.*]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @.omp_target_task_proxy_func(
// CHECK: entry:
// CHECK:   call void @omp_target_depend_..omp_par(i32 %[[VAL_0:.*]])
// CHECK:   ret void
// CHECK: }
