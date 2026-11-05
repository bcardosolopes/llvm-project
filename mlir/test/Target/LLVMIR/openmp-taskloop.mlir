// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

omp.private {type = private} @_QFtestEi_private_i32 : i32

omp.private {type = firstprivate} @_QFtestEa_firstprivate_i32 : i32 copy {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %0 = llvm.load %arg0 : !llvm.ptr -> i32
  llvm.store %0, %arg1 : i32, !llvm.ptr
  omp.yield(%arg1 : !llvm.ptr)
}


llvm.func @_QPtest() {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
  %3 = llvm.alloca %0 x i32 {bindc_name = "a"} : (i64) -> !llvm.ptr
  %6 = llvm.mlir.constant(20 : i32) : i32
  llvm.store %6, %3 : i32, !llvm.ptr
  %7 = llvm.mlir.constant(1 : i32) : i32
  %8 = llvm.mlir.constant(5 : i32) : i32
  %9 = llvm.mlir.constant(1 : i32) : i32
  omp.taskloop private(@_QFtestEa_firstprivate_i32 %3 -> %arg0, @_QFtestEi_private_i32 %1 -> %arg1 : !llvm.ptr, !llvm.ptr) {
    omp.loop_nest (%arg2) : i32 = (%7) to (%8) inclusive step (%9) {
      llvm.store %arg2, %arg1 : i32, !llvm.ptr
      %10 = llvm.load %arg0 : !llvm.ptr -> i32
      %11 = llvm.mlir.constant(1 : i32) : i32
      %12 = llvm.add %10, %11 : i32
      llvm.store %12, %arg0 : i32, !llvm.ptr
      omp.yield
    }
  }
  llvm.return
}

// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @_QPtest(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} i64, i64, i64, ptr }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, i64 1, align 4
// CHECK:   store i32 20, ptr %[[VAL_2]], align 4
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.private.init:
// CHECK:   %[[VAL_5:.*]] = tail call ptr @malloc(i64 4)
// CHECK:   %[[VAL_6:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_5]], i32 0, i32 0
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.private.copy1:
// CHECK:   %[[VAL_9:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   store i32 %[[VAL_9]], ptr %[[VAL_6]], align 4
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.taskloop.start:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_12:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store i64 0, ptr %[[VAL_12]], align 4
// CHECK:   %[[VAL_13:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   store i64 4, ptr %[[VAL_13]], align 4
// CHECK:   %[[VAL_14:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   store i64 1, ptr %[[VAL_14]], align 4
// CHECK:   %[[VAL_15:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 3
// CHECK:   store ptr %[[VAL_5]], ptr %[[VAL_15]], align 8
// CHECK:   %[[VAL_16:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_taskgroup(ptr @1, i32 %[[VAL_16]])
// CHECK:   %[[VAL_17:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_16]], i32 1, i64 40, i64 32, ptr @_QPtest..omp_par)
// CHECK:   %[[VAL_18:.*]] = load ptr, ptr %[[VAL_17]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_18]], ptr align 1 %[[VAL_0]], i64 32, i1 false)
// CHECK:   %[[VAL_19:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_18]], i32 0, i32 0
// CHECK:   %[[VAL_20:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_18]], i32 0, i32 1
// CHECK:   %[[VAL_21:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_18]], i32 0, i32 2
// CHECK:   %[[VAL_22:.*]] = load i64, ptr %[[VAL_21]], align 4
// CHECK:   call void @__kmpc_taskloop(ptr @1, i32 %[[VAL_16]], ptr %[[VAL_17]], i32 1, ptr %[[VAL_19]], ptr %[[VAL_20]], i64 %[[VAL_22]], i32 1, i32 0, i64 0, ptr @omp_taskloop_dup)
// CHECK:   call void @__kmpc_end_taskgroup(ptr @1, i32 %[[VAL_16]])
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: taskloop.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @_QPtest..omp_par(
// CHECK: taskloop.alloca:
// CHECK:   %[[VAL_0:.*]] = load ptr, ptr %[[VAL_1:.*]], align 8
// CHECK:   %[[VAL_2:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   %[[VAL_3:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_4:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   %[[VAL_5:.*]] = load i64, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_6:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   %[[VAL_7:.*]] = load i64, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_8:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 3
// CHECK:   %[[VAL_9:.*]] = load ptr, ptr %[[VAL_8]], align 8, !align !1
// CHECK:   %[[VAL_10:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: taskloop.body:
// CHECK:   %[[VAL_12:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_9]], i32 0, i32 0
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.taskloop.region:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   %[[VAL_15:.*]] = sub i64 %[[VAL_5]], %[[VAL_3]]
// CHECK:   %[[VAL_16:.*]] = sdiv i64 %[[VAL_15]], %[[VAL_7]]
// CHECK:   %[[VAL_17:.*]] = add i64 %[[VAL_16]], 1
// CHECK:   %[[VAL_18:.*]] = trunc i64 %[[VAL_17]] to i32
// CHECK:   %[[VAL_19:.*]] = trunc i64 %[[VAL_3]] to i32
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_21:.*]] = phi i32 [ 0, %[[VAL_14]] ], [ %[[VAL_22:.*]], %[[VAL_23:.*]] ]
// CHECK:   br label %[[VAL_24:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_25:.*]] = icmp ult i32 %[[VAL_21]], %[[VAL_18]]
// CHECK:   br i1 %[[VAL_25]], label %[[VAL_26:.*]], label %[[VAL_27:.*]]
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_29:.*]]
// CHECK: omp.region.cont:
// CHECK:   tail call void @free(ptr %[[VAL_9]])
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_31:.*]] = add i32 %[[VAL_21]], %[[VAL_19]]
// CHECK:   %[[VAL_32:.*]] = mul i32 %[[VAL_31]], 1
// CHECK:   %[[VAL_33:.*]] = add i32 %[[VAL_32]], 1
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_33]], ptr %[[VAL_10]], align 4
// CHECK:   %[[VAL_35:.*]] = load i32, ptr %[[VAL_12]], align 4
// CHECK:   %[[VAL_36:.*]] = add i32 %[[VAL_35]], 1
// CHECK:   store i32 %[[VAL_36]], ptr %[[VAL_12]], align 4
// CHECK:   br label %[[VAL_37:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_22]] = add nuw i32 %[[VAL_21]], 1
// CHECK:   br label %[[VAL_20]]
// CHECK: taskloop.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_taskloop_dup(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} %[[VAL_1:.*]], {{[{]}} i64, i64, i64, ptr } }, ptr %[[VAL_2:.*]], i32 0, i32 1
// CHECK:   %[[VAL_3:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 3
// CHECK:   %[[VAL_4:.*]] = getelementptr {{[{]}} %[[VAL_1]], {{[{]}} i64, i64, i64, ptr } }, ptr %[[VAL_5:.*]], i32 0, i32 1
// CHECK:   %[[VAL_6:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_4]], i32 0, i32 3
// CHECK:   %[[VAL_7:.*]] = load ptr, ptr %[[VAL_6]], align 8
// CHECK:   %[[VAL_8:.*]] = tail call ptr @malloc(i64 4)
// CHECK:   store ptr %[[VAL_8]], ptr %[[VAL_3]], align 8
// CHECK:   %[[VAL_9:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_7]], i32 0, i32 0
// CHECK:   %[[VAL_10:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_8]], i32 0, i32 0
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.copy:
// CHECK:   %[[VAL_12:.*]] = load i32, ptr %[[VAL_9]], align 4
// CHECK:   store i32 %[[VAL_12]], ptr %[[VAL_10]], align 4
// CHECK:   ret void
// CHECK: }
