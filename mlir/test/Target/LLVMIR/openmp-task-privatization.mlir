// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

omp.private {type = private} @privatizer : i32

omp.private {type = firstprivate} @firstprivatizer : i32 copy {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %0 = llvm.load %arg0 : !llvm.ptr -> i32
  llvm.store %0, %arg1 : i32, !llvm.ptr
  omp.yield(%arg1 : !llvm.ptr)
}

llvm.func @task_privatization_test() {
  %c0 = llvm.mlir.constant(0: i32) : i32
  %c1 = llvm.mlir.constant(1: i32) : i32
  %0 = llvm.alloca %c1 x i32 : (i32) -> !llvm.ptr
  %1 = llvm.alloca %c1 x i32 : (i32) -> !llvm.ptr
  llvm.store %c0, %0 : i32, !llvm.ptr
  llvm.store %c1, %1 : i32, !llvm.ptr

  omp.task private(@privatizer %0 -> %arg0, @firstprivatizer %1 -> %arg1 : !llvm.ptr, !llvm.ptr) {
    %2 = llvm.load %arg1 : !llvm.ptr -> i32
    llvm.store %2, %arg0 : i32, !llvm.ptr
    omp.terminator
  }
  llvm.return
}

// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @task_privatization_test(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   store i32 0, ptr %[[VAL_1]], align 4
// CHECK:   store i32 1, ptr %[[VAL_2]], align 4
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
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_12:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_5]], ptr %[[VAL_12]], align 8
// CHECK:   %[[VAL_13:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_14:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_13]], i32 1, i64 40, i64 8, ptr @task_privatization_test..omp_par)
// CHECK:   %[[VAL_15:.*]] = load ptr, ptr %[[VAL_14]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_15]], ptr align 1 %[[VAL_0]], i64 8, i1 false)
// CHECK:   %[[VAL_16:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_13]], ptr %[[VAL_14]])
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: task.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @task_privatization_test..omp_par(
// CHECK: task.alloca:
// CHECK:   %[[VAL_0:.*]] = load ptr, ptr %[[VAL_1:.*]], align 8
// CHECK:   %[[VAL_2:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   %[[VAL_3:.*]] = load ptr, ptr %[[VAL_2]], align 8, !align !1
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: task.body:
// CHECK:   %[[VAL_6:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_3]], i32 0, i32 0
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.task.region:
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   store i32 %[[VAL_8]], ptr %[[VAL_4]], align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.cont:
// CHECK:   tail call void @free(ptr %[[VAL_3]])
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
