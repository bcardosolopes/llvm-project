// RUN: mlir-translate --mlir-to-llvmir %s | FileCheck %s

// Regression test for a compiler crash. Ensure that the insertion point is set
// correctly when triggering the charbox hack multiple times.
// Nonsense test code to minimally reproduce the issue.

module {
  llvm.func @free(!llvm.ptr)
  llvm.func @malloc(i64) -> !llvm.ptr
  omp.private {type = private} @_QFEc2_private_box_heap_c8xU : !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> init {
  ^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
    %0 = llvm.mlir.constant(24 : i32) : i32
    %1 = llvm.mlir.constant(0 : i64) : i64
    %2 = llvm.mlir.constant(1 : i32) : i32
    %3 = llvm.alloca %2 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> {alignment = 8 : i64} : (i32) -> !llvm.ptr
    "llvm.intr.memcpy"(%3, %arg0, %0) <{isVolatile = false}> : (!llvm.ptr, !llvm.ptr, i32) -> ()
    %6 = llvm.ptrtoint %arg0 : !llvm.ptr to i64
    %7 = llvm.icmp "eq" %6, %1 : i64
    llvm.cond_br %7, ^bb1, ^bb2
  ^bb1:  // pred: ^bb0
    llvm.br ^bb3
  ^bb2:  // pred: ^bb0
    llvm.br ^bb3
  ^bb3:  // 2 preds: ^bb1, ^bb2
    omp.yield(%arg1 : !llvm.ptr)
  } dealloc {
  ^bb0(%arg0: !llvm.ptr):
    omp.yield
  }
  omp.private {type = private} @_QFEc1_private_box_ptr_c8xU : !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> init {
  ^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
    %0 = llvm.mlir.constant(24 : i32) : i32
    %1 = llvm.mlir.constant(1 : i32) : i32
    %2 = llvm.alloca %1 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> {alignment = 8 : i64} : (i32) -> !llvm.ptr
    "llvm.intr.memcpy"(%2, %arg0, %0) <{isVolatile = false}> : (!llvm.ptr, !llvm.ptr, i32) -> ()
    omp.yield(%arg1 : !llvm.ptr)
  }
  llvm.func @_QQmain() {
    %0 = llvm.mlir.constant(1 : i64) : i64
    %1 = llvm.alloca %0 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> {bindc_name = "c2"} : (i64) -> !llvm.ptr
    %2 = llvm.alloca %0 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> {bindc_name = "c1"} : (i64) -> !llvm.ptr
    omp.task private(@_QFEc1_private_box_ptr_c8xU %2 -> %arg0, @_QFEc2_private_box_heap_c8xU %1 -> %arg1 : !llvm.ptr, !llvm.ptr) {
      omp.terminator
    }
    llvm.return
  }
}

// ...

// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @_QQmain(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, i64 1, align 8
// CHECK:   %[[VAL_2:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, i64 1, align 8
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.private.init:
// CHECK:   %[[VAL_5:.*]] = tail call ptr @malloc(i64 48)
// CHECK:   %[[VAL_6:.*]] = getelementptr {{[{]}} {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, {{[{]}} ptr, i64, i32, i8, i8, i8, i8 } }, ptr %[[VAL_5]], i32 0, i32 0
// CHECK:   %[[VAL_7:.*]] = getelementptr {{[{]}} {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, {{[{]}} ptr, i64, i32, i8, i8, i8, i8 } }, ptr %[[VAL_5]], i32 0, i32 1
// CHECK:   %[[VAL_8:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i32(ptr %[[VAL_8]], ptr %[[VAL_2]], i32 24, i1 false)
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.private.init4:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.private.init3:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init2:
// CHECK:   br label %[[VAL_11]]
// CHECK: omp.private.init1:
// CHECK:   %[[VAL_12:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i32(ptr %[[VAL_12]], ptr %[[VAL_1]], i32 24, i1 false)
// CHECK:   %[[VAL_13:.*]] = ptrtoint ptr %[[VAL_1]] to i64
// CHECK:   %[[VAL_14:.*]] = icmp eq i64 %[[VAL_13]], 0
// CHECK:   br i1 %[[VAL_14]], label %[[VAL_15:.*]], label %[[VAL_16:.*]]
// CHECK: omp.region.cont:
// CHECK:   %[[VAL_17:.*]] = phi ptr [ %[[VAL_7]], %[[VAL_11]] ]
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_21:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_5]], ptr %[[VAL_21]], align 8
// CHECK:   %[[VAL_22:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_23:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_22]], i32 1, i64 40, i64 8, ptr @_QQmain..omp_par)
// CHECK:   %[[VAL_24:.*]] = load ptr, ptr %[[VAL_23]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_24]], ptr align 1 %[[VAL_0]], i64 8, i1 false)
// CHECK:   %[[VAL_25:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_22]], ptr %[[VAL_23]])
// CHECK:   br label %[[VAL_26:.*]]
// CHECK: task.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @_QQmain..omp_par(
// CHECK: task.alloca:
// CHECK:   %[[VAL_0:.*]] = load ptr, ptr %[[VAL_1:.*]], align 8
// CHECK:   %[[VAL_2:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   %[[VAL_3:.*]] = load ptr, ptr %[[VAL_2]], align 8, !align !1
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: task.body:
// CHECK:   %[[VAL_5:.*]] = getelementptr {{[{]}} {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, {{[{]}} ptr, i64, i32, i8, i8, i8, i8 } }, ptr %[[VAL_3]], i32 0, i32 0
// CHECK:   %[[VAL_6:.*]] = getelementptr {{[{]}} {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, {{[{]}} ptr, i64, i32, i8, i8, i8, i8 } }, ptr %[[VAL_3]], i32 0, i32 1
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.task.region:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont5:
// CHECK:   tail call void @free(ptr %[[VAL_3]])
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
