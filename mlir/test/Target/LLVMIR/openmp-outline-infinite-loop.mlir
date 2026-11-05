// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

// Test that trying to outline an infinite loop doesn't lead to an assertion
// failure.

llvm.func @parallel_infinite_loop() -> () {
  omp.parallel {
    llvm.br ^bb1
  ^bb1:
    llvm.br ^bb1
  }
  llvm.return
}

// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @parallel_infinite_loop(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @parallel_infinite_loop..omp_par)
// CHECK:   unreachable
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: .fini:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: }
// CHECK-LABEL: define internal void @parallel_infinite_loop..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.par.region2:
// CHECK:   br label %[[VAL_7]]
// CHECK: }
