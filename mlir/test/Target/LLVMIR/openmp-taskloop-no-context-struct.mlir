// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

// Regression check for a taskloop with private variables but none of the
// private variables go into the context struct.

omp.private {type = private} @_QFtestEi_private_i32 : i32
omp.private {type = private} @_QFtestEt2_private_i32 : i32
omp.private {type = private} @_QFtestEt1_private_i32 : i32
llvm.func @_QPtest() {
  %0 = llvm.mlir.constant(1 : i32) : i32
  %1 = llvm.mlir.constant(20 : i32) : i32
  %2 = llvm.mlir.constant(1 : i64) : i64
  %3 = llvm.alloca %2 x i32 {bindc_name = "t2"} : (i64) -> !llvm.ptr
  %4 = llvm.alloca %2 x i32 {bindc_name = "t1"} : (i64) -> !llvm.ptr
  %5 = llvm.alloca %2 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
  omp.taskloop private(@_QFtestEt1_private_i32 %4 -> %arg0, @_QFtestEt2_private_i32 %3 -> %arg1, @_QFtestEi_private_i32 %5 -> %arg2 : !llvm.ptr, !llvm.ptr, !llvm.ptr) {
    omp.loop_nest (%arg3) : i32 = (%0) to (%1) inclusive step (%0) {
      llvm.store %arg3, %arg2 : i32, !llvm.ptr
      omp.yield
    }
  }
  llvm.return
}
// No task context structure:

// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @_QPtest(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} i64, i64, i64 }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, i64 1, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.taskloop.start:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_9:.*]] = getelementptr {{[{]}} i64, i64, i64 }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store i64 0, ptr %[[VAL_9]], align 4
// CHECK:   %[[VAL_10:.*]] = getelementptr {{[{]}} i64, i64, i64 }, ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   store i64 19, ptr %[[VAL_10]], align 4
// CHECK:   %[[VAL_11:.*]] = getelementptr {{[{]}} i64, i64, i64 }, ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   store i64 1, ptr %[[VAL_11]], align 4
// CHECK:   %[[VAL_12:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_taskgroup(ptr @1, i32 %[[VAL_12]])
// CHECK:   %[[VAL_13:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_12]], i32 1, i64 40, i64 24, ptr @_QPtest..omp_par)
// CHECK:   %[[VAL_14:.*]] = load ptr, ptr %[[VAL_13]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_14]], ptr align 1 %[[VAL_0]], i64 24, i1 false)
// CHECK:   %[[VAL_15:.*]] = getelementptr {{[{]}} i64, i64, i64 }, ptr %[[VAL_14]], i32 0, i32 0
// CHECK:   %[[VAL_16:.*]] = getelementptr {{[{]}} i64, i64, i64 }, ptr %[[VAL_14]], i32 0, i32 1
// CHECK:   %[[VAL_17:.*]] = getelementptr {{[{]}} i64, i64, i64 }, ptr %[[VAL_14]], i32 0, i32 2
// CHECK:   %[[VAL_18:.*]] = load i64, ptr %[[VAL_17]], align 4
// CHECK:   call void @__kmpc_taskloop(ptr @1, i32 %[[VAL_12]], ptr %[[VAL_13]], i32 1, ptr %[[VAL_15]], ptr %[[VAL_16]], i64 %[[VAL_18]], i32 1, i32 0, i64 0, ptr null)
// CHECK:   call void @__kmpc_end_taskgroup(ptr @1, i32 %[[VAL_12]])
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: taskloop.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @_QPtest..omp_par(
// CHECK: taskloop.alloca:
// CHECK:   %[[VAL_0:.*]] = load ptr, ptr %[[VAL_1:.*]], align 8
// CHECK:   %[[VAL_2:.*]] = getelementptr {{[{]}} i64, i64, i64 }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   %[[VAL_3:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_4:.*]] = getelementptr {{[{]}} i64, i64, i64 }, ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   %[[VAL_5:.*]] = load i64, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_6:.*]] = getelementptr {{[{]}} i64, i64, i64 }, ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   %[[VAL_7:.*]] = load i64, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_8:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_9:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_10:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: taskloop.body:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.taskloop.region:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   %[[VAL_14:.*]] = sub i64 %[[VAL_5]], %[[VAL_3]]
// CHECK:   %[[VAL_15:.*]] = sdiv i64 %[[VAL_14]], %[[VAL_7]]
// CHECK:   %[[VAL_16:.*]] = add i64 %[[VAL_15]], 1
// CHECK:   %[[VAL_17:.*]] = trunc i64 %[[VAL_16]] to i32
// CHECK:   %[[VAL_18:.*]] = trunc i64 %[[VAL_3]] to i32
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_20:.*]] = phi i32 [ 0, %[[VAL_13]] ], [ %[[VAL_21:.*]], %[[VAL_22:.*]] ]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_24:.*]] = icmp ult i32 %[[VAL_20]], %[[VAL_17]]
// CHECK:   br i1 %[[VAL_24]], label %[[VAL_25:.*]], label %[[VAL_26:.*]]
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_29:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_30:.*]] = add i32 %[[VAL_20]], %[[VAL_18]]
// CHECK:   %[[VAL_31:.*]] = mul i32 %[[VAL_30]], 1
// CHECK:   %[[VAL_32:.*]] = add i32 %[[VAL_31]], 1
// CHECK:   br label %[[VAL_33:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_32]], ptr %[[VAL_10]], align 4
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_22]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_21]] = add nuw i32 %[[VAL_20]], 1
// CHECK:   br label %[[VAL_19]]
// CHECK: taskloop.exit.exitStub:
// CHECK:   ret void
// CHECK: }
