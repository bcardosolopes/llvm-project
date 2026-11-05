// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

// Tests `target ... nowait` when code gen targets the host rather than a
// device.

module attributes {omp.is_target_device = false} {
  llvm.func @omp_target_nowait_() {
    %0 = llvm.mlir.constant(1 : i64) : i64
    %1 = llvm.alloca %0 x f32 {bindc_name = "x"} : (i64) -> !llvm.ptr
    %3 = omp.map.info var_ptr(%1 : !llvm.ptr, f32) map_clauses(tofrom) capture(ByRef) -> !llvm.ptr {name = "x"}
    omp.target nowait map_entries(%3 -> %arg0 : !llvm.ptr) {
      %4 = llvm.mlir.constant(5.000000e+00 : f32) : f32
      llvm.store %4, %arg0 : f32, !llvm.ptr
      omp.terminator
    }
    llvm.return
  }
}

// https://github.com/llvm/llvm-project/issues/126949 exposes two issues
// 1. Empty target task proxy functions
// 2. When 1 fixed, it leads to a second problem of calling the omp target kernel twice
//    Once via the target task proxy function and a second time after the target task is done.
// The following checks check problem #2.
// functions. The following checks tests the fix for this issue.

// Verify that we directly emit a call to the "target" region's body from the
// parent function of the the `omp.target` op.


// The following check test for the fix of problem #1 as described in https://github.com/llvm/llvm-project/issues/126949

// CHECK: @0 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @1 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @1 }, align 8
// CHECK-LABEL: define void @omp_target_nowait_(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca float, i64 1, align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_4:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_1]], ptr %[[VAL_4]], align 8
// CHECK:   %[[VAL_5:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   %[[VAL_6:.*]] = call ptr @__kmpc_omp_target_task_alloc(ptr @2, i32 %[[VAL_5]], i32 0, i64 40, i64 8, ptr @.omp_target_task_proxy_func, i64 -1)
// CHECK:   %[[VAL_7:.*]] = getelementptr inbounds nuw %[[VAL_8:.*]], ptr %[[VAL_6]], i32 0, i32 0
// CHECK:   %[[VAL_9:.*]] = load ptr, ptr %[[VAL_7]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_9]], ptr align 1 %[[VAL_0]], i64 8, i1 false)
// CHECK:   %[[VAL_10:.*]] = call i32 @__kmpc_omp_task(ptr @2, i32 %[[VAL_5]], ptr %[[VAL_6]])
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: target.task.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_target_nowait_..omp_par(
// CHECK: target.task.alloca:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_1:.*]], i32 0, i32 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8, !align !1
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: target.task.body:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_omp_target_nowait__l{{.*}}(ptr %[[VAL_2]])
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: target.task.cont.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_omp_target_nowait__l{{.*}}(
// CHECK: entry:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.target:
// CHECK:   store float 5.000000e+00, ptr %[[VAL_3:.*]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @.omp_target_task_proxy_func(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   %[[VAL_1:.*]] = getelementptr inbounds nuw %[[VAL_2:.*]], ptr %[[VAL_3:.*]], i32 0, i32 0
// CHECK:   %[[VAL_4:.*]] = load ptr, ptr %[[VAL_1]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 8 %[[VAL_0]], ptr align 1 %[[VAL_4]], i64 8, i1 false)
// CHECK:   call void @omp_target_nowait_..omp_par(i32 %[[VAL_5:.*]], ptr %[[VAL_0]])
// CHECK:   ret void
// CHECK: }
