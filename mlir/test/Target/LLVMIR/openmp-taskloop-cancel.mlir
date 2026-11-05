// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

omp.private {type = private} @_QFtestEi_private_i32 : i32
omp.private {type = firstprivate} @_QFtestEarg_firstprivate_i32 : i32 init {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  llvm.call @_init(%arg0, %arg1) : (!llvm.ptr, !llvm.ptr) -> ()
  omp.yield(%arg1 : !llvm.ptr)
} copy {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  llvm.call @_copy(%arg0, %arg1) : (!llvm.ptr, !llvm.ptr) -> ()
  omp.yield(%arg1 : !llvm.ptr)
} dealloc {
^bb0(%arg0: !llvm.ptr):
  llvm.call @_dealloc(%arg0) : (!llvm.ptr) -> ()
  omp.yield
}

// Test cancelling implicit taskgroup created by taskloop
llvm.func @_QPtest(%arg0: !llvm.ptr {fir.bindc_name = "arg", llvm.noalias, llvm.nocapture}) {
  %0 = llvm.mlir.constant(1 : i32) : i32
  %1 = llvm.mlir.constant(100 : i32) : i32
  %2 = llvm.mlir.constant(1 : i64) : i64
  %3 = llvm.alloca %2 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
  omp.taskloop private(@_QFtestEarg_firstprivate_i32 %arg0 -> %arg1, @_QFtestEi_private_i32 %3 -> %arg2 : !llvm.ptr, !llvm.ptr) {
    omp.loop_nest (%arg3) : i32 = (%0) to (%1) inclusive step (%0) {
      llvm.store %arg3, %arg2 : i32, !llvm.ptr
      llvm.call @_QPbefore(%arg1) : (!llvm.ptr) -> ()
      omp.cancel cancellation_construct_type(taskgroup)
      llvm.call @_QPafter(%arg1) : (!llvm.ptr) -> ()
      omp.yield
    }
  }
  llvm.return
}


// Tesk cancelling explicit taskgroup enclosing taskloop
llvm.func @_QPtest2(%arg0: !llvm.ptr {fir.bindc_name = "arg", llvm.noalias, llvm.nocapture}) {
  %0 = llvm.mlir.constant(1 : i32) : i32
  %1 = llvm.mlir.constant(100 : i32) : i32
  %2 = llvm.mlir.constant(1 : i64) : i64
  %3 = llvm.alloca %2 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
  omp.taskgroup {
    omp.taskloop nogroup private(@_QFtestEarg_firstprivate_i32 %arg0 -> %arg1, @_QFtestEi_private_i32 %3 -> %arg2 : !llvm.ptr, !llvm.ptr) {
      omp.loop_nest (%arg3) : i32 = (%0) to (%1) inclusive step (%0) {
        llvm.store %arg3, %arg2 : i32, !llvm.ptr
        llvm.call @_QPbefore(%arg1) : (!llvm.ptr) -> ()
        omp.cancel cancellation_construct_type(taskgroup)
        llvm.call @_QPafter(%arg1) : (!llvm.ptr) -> ()
        omp.yield
      }
    }
    omp.terminator
  }
  llvm.return
}


// Test if clause
llvm.func @_QPtest3(%arg0: !llvm.ptr {fir.bindc_name = "arg", llvm.noalias, llvm.nocapture}) {
  %0 = llvm.mlir.constant(1 : i32) : i32
  %1 = llvm.mlir.constant(100 : i32) : i32
  %2 = llvm.mlir.constant(1 : i64) : i64
  %3 = llvm.alloca %2 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
  omp.taskloop private(@_QFtestEarg_firstprivate_i32 %arg0 -> %arg1, @_QFtestEi_private_i32 %3 -> %arg2 : !llvm.ptr, !llvm.ptr) {
    omp.loop_nest (%arg3) : i32 = (%0) to (%1) inclusive step (%0) {
      llvm.store %arg3, %arg2 : i32, !llvm.ptr
      llvm.call @_QPbefore(%arg1) : (!llvm.ptr) -> ()
      %true = llvm.mlir.constant(1 : i1) : i1
      omp.cancel cancellation_construct_type(taskgroup) if(%true)
      llvm.call @_QPafter(%arg1) : (!llvm.ptr) -> ()
      omp.yield
    }
  }
  llvm.return
}

// IF CLAUSE:

llvm.func @_QPbefore(!llvm.ptr) attributes {sym_visibility = "private"}
llvm.func @_QPafter(!llvm.ptr) attributes {sym_visibility = "private"}
llvm.func @_init(!llvm.ptr, !llvm.ptr) attributes {sym_visibility = "private"}
llvm.func @_copy(!llvm.ptr, !llvm.ptr) attributes {sym_visibility = "private"}
llvm.func @_dealloc(!llvm.ptr) attributes {sym_visibility = "private"}

// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @_QPtest(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} i64, i64, i64, ptr }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i32, i64 1, align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.private.init:
// CHECK:   %[[VAL_4:.*]] = tail call ptr @malloc(i64 4)
// CHECK:   %[[VAL_5:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_4]], i32 0, i32 0
// CHECK:   call void @_init(ptr %[[VAL_6:.*]], ptr %[[VAL_5]])
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.private.copy1:
// CHECK:   call void @_copy(ptr %[[VAL_6]], ptr %[[VAL_5]])
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.taskloop.start:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_11:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store i64 0, ptr %[[VAL_11]], align 4
// CHECK:   %[[VAL_12:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   store i64 99, ptr %[[VAL_12]], align 4
// CHECK:   %[[VAL_13:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   store i64 1, ptr %[[VAL_13]], align 4
// CHECK:   %[[VAL_14:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 3
// CHECK:   store ptr %[[VAL_4]], ptr %[[VAL_14]], align 8
// CHECK:   %[[VAL_15:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_taskgroup(ptr @1, i32 %[[VAL_15]])
// CHECK:   %[[VAL_16:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_15]], i32 1, i64 40, i64 32, ptr @_QPtest..omp_par)
// CHECK:   %[[VAL_17:.*]] = load ptr, ptr %[[VAL_16]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_17]], ptr align 1 %[[VAL_0]], i64 32, i1 false)
// CHECK:   %[[VAL_18:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_17]], i32 0, i32 0
// CHECK:   %[[VAL_19:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_17]], i32 0, i32 1
// CHECK:   %[[VAL_20:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_17]], i32 0, i32 2
// CHECK:   %[[VAL_21:.*]] = load i64, ptr %[[VAL_20]], align 4
// CHECK:   call void @__kmpc_taskloop(ptr @1, i32 %[[VAL_15]], ptr %[[VAL_16]], i32 1, ptr %[[VAL_18]], ptr %[[VAL_19]], i64 %[[VAL_21]], i32 1, i32 0, i64 0, ptr @omp_taskloop_dup)
// CHECK:   call void @__kmpc_end_taskgroup(ptr @1, i32 %[[VAL_15]])
// CHECK:   br label %[[VAL_22:.*]]
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
// CHECK:   call void @_dealloc(ptr %[[VAL_12]])
// CHECK:   tail call void @free(ptr %[[VAL_9]])
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_31:.*]] = add i32 %[[VAL_21]], %[[VAL_19]]
// CHECK:   %[[VAL_32:.*]] = mul i32 %[[VAL_31]], 1
// CHECK:   %[[VAL_33:.*]] = add i32 %[[VAL_32]], 1
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_33]], ptr %[[VAL_10]], align 4
// CHECK:   call void @_QPbefore(ptr %[[VAL_12]])
// CHECK:   %[[VAL_35:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_36:.*]] = call i32 @__kmpc_cancel(ptr @1, i32 %[[VAL_35]], i32 4)
// CHECK:   %[[VAL_37:.*]] = icmp eq i32 %[[VAL_36]], 0
// CHECK:   br i1 %[[VAL_37]], label %[[VAL_38:.*]], label %[[VAL_39:.*]]
// CHECK: omp.loop_nest.region.cncl:
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp.loop_nest.region.split:
// CHECK:   call void @_QPafter(ptr %[[VAL_12]])
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_22]] = add nuw i32 %[[VAL_21]], 1
// CHECK:   br label %[[VAL_20]]
// CHECK: taskloop.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPtest2(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} i64, i64, i64, ptr }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i32, i64 1, align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_taskgroup(ptr @1, i32 %[[VAL_3]])
// CHECK:   %[[VAL_4:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.taskgroup.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.private.init:
// CHECK:   %[[VAL_7:.*]] = tail call ptr @malloc(i64 4)
// CHECK:   %[[VAL_8:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_7]], i32 0, i32 0
// CHECK:   call void @_init(ptr %[[VAL_9:.*]], ptr %[[VAL_8]])
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.copy2:
// CHECK:   call void @_copy(ptr %[[VAL_9]], ptr %[[VAL_8]])
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: taskgroup.exit:
// CHECK:   call void @__kmpc_end_taskgroup(ptr @1, i32 %[[VAL_3]])
// CHECK:   ret void
// CHECK: omp.taskloop.start:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_15:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store i64 0, ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_16:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   store i64 99, ptr %[[VAL_16]], align 4
// CHECK:   %[[VAL_17:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   store i64 1, ptr %[[VAL_17]], align 4
// CHECK:   %[[VAL_18:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 3
// CHECK:   store ptr %[[VAL_7]], ptr %[[VAL_18]], align 8
// CHECK:   %[[VAL_19:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_20:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_19]], i32 1, i64 40, i64 32, ptr @_QPtest2..omp_par)
// CHECK:   %[[VAL_21:.*]] = load ptr, ptr %[[VAL_20]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_21]], ptr align 1 %[[VAL_0]], i64 32, i1 false)
// CHECK:   %[[VAL_22:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_21]], i32 0, i32 0
// CHECK:   %[[VAL_23:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_21]], i32 0, i32 1
// CHECK:   %[[VAL_24:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_21]], i32 0, i32 2
// CHECK:   %[[VAL_25:.*]] = load i64, ptr %[[VAL_24]], align 4
// CHECK:   call void @__kmpc_taskloop(ptr @1, i32 %[[VAL_19]], ptr %[[VAL_20]], i32 1, ptr %[[VAL_22]], ptr %[[VAL_23]], i64 %[[VAL_25]], i32 1, i32 0, i64 0, ptr @omp_taskloop_dup.1)
// CHECK:   br label %[[VAL_26:.*]]
// CHECK: taskloop.exit:
// CHECK:   br label %[[VAL_27:.*]]
// CHECK: }
// CHECK-LABEL: define internal void @_QPtest2..omp_par(
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
// CHECK: omp.region.cont3:
// CHECK:   call void @_dealloc(ptr %[[VAL_12]])
// CHECK:   tail call void @free(ptr %[[VAL_9]])
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_31:.*]] = add i32 %[[VAL_21]], %[[VAL_19]]
// CHECK:   %[[VAL_32:.*]] = mul i32 %[[VAL_31]], 1
// CHECK:   %[[VAL_33:.*]] = add i32 %[[VAL_32]], 1
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_33]], ptr %[[VAL_10]], align 4
// CHECK:   call void @_QPbefore(ptr %[[VAL_12]])
// CHECK:   %[[VAL_35:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_36:.*]] = call i32 @__kmpc_cancel(ptr @1, i32 %[[VAL_35]], i32 4)
// CHECK:   %[[VAL_37:.*]] = icmp eq i32 %[[VAL_36]], 0
// CHECK:   br i1 %[[VAL_37]], label %[[VAL_38:.*]], label %[[VAL_39:.*]]
// CHECK: omp.loop_nest.region.cncl:
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_29]]
// CHECK: omp.loop_nest.region.split:
// CHECK:   call void @_QPafter(ptr %[[VAL_12]])
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.region.cont4:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_22]] = add nuw i32 %[[VAL_21]], 1
// CHECK:   br label %[[VAL_20]]
// CHECK: taskloop.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPtest3(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} i64, i64, i64, ptr }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i32, i64 1, align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.private.init:
// CHECK:   %[[VAL_4:.*]] = tail call ptr @malloc(i64 4)
// CHECK:   %[[VAL_5:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_4]], i32 0, i32 0
// CHECK:   call void @_init(ptr %[[VAL_6:.*]], ptr %[[VAL_5]])
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.private.copy1:
// CHECK:   call void @_copy(ptr %[[VAL_6]], ptr %[[VAL_5]])
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.taskloop.start:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_11:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store i64 0, ptr %[[VAL_11]], align 4
// CHECK:   %[[VAL_12:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   store i64 99, ptr %[[VAL_12]], align 4
// CHECK:   %[[VAL_13:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   store i64 1, ptr %[[VAL_13]], align 4
// CHECK:   %[[VAL_14:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_0]], i32 0, i32 3
// CHECK:   store ptr %[[VAL_4]], ptr %[[VAL_14]], align 8
// CHECK:   %[[VAL_15:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_taskgroup(ptr @1, i32 %[[VAL_15]])
// CHECK:   %[[VAL_16:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_15]], i32 1, i64 40, i64 32, ptr @_QPtest3..omp_par)
// CHECK:   %[[VAL_17:.*]] = load ptr, ptr %[[VAL_16]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_17]], ptr align 1 %[[VAL_0]], i64 32, i1 false)
// CHECK:   %[[VAL_18:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_17]], i32 0, i32 0
// CHECK:   %[[VAL_19:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_17]], i32 0, i32 1
// CHECK:   %[[VAL_20:.*]] = getelementptr {{[{]}} i64, i64, i64, ptr }, ptr %[[VAL_17]], i32 0, i32 2
// CHECK:   %[[VAL_21:.*]] = load i64, ptr %[[VAL_20]], align 4
// CHECK:   call void @__kmpc_taskloop(ptr @1, i32 %[[VAL_15]], ptr %[[VAL_16]], i32 1, ptr %[[VAL_18]], ptr %[[VAL_19]], i64 %[[VAL_21]], i32 1, i32 0, i64 0, ptr @omp_taskloop_dup.2)
// CHECK:   call void @__kmpc_end_taskgroup(ptr @1, i32 %[[VAL_15]])
// CHECK:   br label %[[VAL_22:.*]]
// CHECK: taskloop.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @_QPtest3..omp_par(
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
// CHECK:   call void @_dealloc(ptr %[[VAL_12]])
// CHECK:   tail call void @free(ptr %[[VAL_9]])
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_31:.*]] = add i32 %[[VAL_21]], %[[VAL_19]]
// CHECK:   %[[VAL_32:.*]] = mul i32 %[[VAL_31]], 1
// CHECK:   %[[VAL_33:.*]] = add i32 %[[VAL_32]], 1
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_33]], ptr %[[VAL_10]], align 4
// CHECK:   call void @_QPbefore(ptr %[[VAL_12]])
// CHECK:   br i1 true, label %[[VAL_35:.*]], label %[[VAL_36:.*]]
// CHECK: 9:
// CHECK:   %[[VAL_37:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_38:.*]] = call i32 @__kmpc_cancellationpoint(ptr @1, i32 %[[VAL_37]], i32 4)
// CHECK:   %[[VAL_39:.*]] = icmp eq i32 %[[VAL_38]], 0
// CHECK:   br i1 %[[VAL_39]], label %[[VAL_40:.*]], label %[[VAL_41:.*]]
// CHECK: .cncl:
// CHECK:   br label %[[VAL_42:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_29]]
// CHECK: .split:
// CHECK:   br label %[[VAL_43:.*]]
// CHECK: 12:
// CHECK:   call void @_QPafter(ptr %[[VAL_12]])
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_22]] = add nuw i32 %[[VAL_21]], 1
// CHECK:   br label %[[VAL_20]]
// CHECK: 13:
// CHECK:   %[[VAL_45:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_46:.*]] = call i32 @__kmpc_cancel(ptr @1, i32 %[[VAL_45]], i32 4)
// CHECK:   %[[VAL_47:.*]] = icmp eq i32 %[[VAL_46]], 0
// CHECK:   br i1 %[[VAL_47]], label %[[VAL_48:.*]], label %[[VAL_49:.*]]
// CHECK: .cncl5:
// CHECK:   br label %[[VAL_42]]
// CHECK: .split4:
// CHECK:   br label %[[VAL_43]]
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
// CHECK:   call void @_init(ptr %[[VAL_9]], ptr %[[VAL_10]])
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.copy:
// CHECK:   call void @_copy(ptr %[[VAL_9]], ptr %[[VAL_10]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_taskloop_dup.1(
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
// CHECK:   call void @_init(ptr %[[VAL_9]], ptr %[[VAL_10]])
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.copy:
// CHECK:   call void @_copy(ptr %[[VAL_9]], ptr %[[VAL_10]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @omp_taskloop_dup.2(
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
// CHECK:   call void @_init(ptr %[[VAL_9]], ptr %[[VAL_10]])
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.copy:
// CHECK:   call void @_copy(ptr %[[VAL_9]], ptr %[[VAL_10]])
// CHECK:   ret void
// CHECK: }
