// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

llvm.func @caller_()  {
  %c1 = llvm.mlir.constant(1 : i64) : i64
  %x_host = llvm.alloca %c1 x f32 {bindc_name = "x"} : (i64) -> !llvm.ptr
  %i_host = llvm.alloca %c1 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
  %x_map = omp.map.info var_ptr(%x_host : !llvm.ptr, f32) map_clauses(implicit, exit_release_or_enter_alloc) capture(ByCopy) -> !llvm.ptr {name = "x"}
  %i_map = omp.map.info var_ptr(%i_host : !llvm.ptr, i32) map_clauses(implicit, exit_release_or_enter_alloc) capture(ByCopy) -> !llvm.ptr {name = "i"}
  omp.target map_entries(%x_map -> %x_arg, %i_map -> %i_arg : !llvm.ptr, !llvm.ptr) {
    %1 = llvm.load %i_arg : !llvm.ptr -> i32
    %2 = llvm.sitofp %1 : i32 to f32
    llvm.store %2, %x_arg : f32, !llvm.ptr
    // The call instruction uses %x_arg more than once. Hence modifying users
    // while iterating them invalidates the iteration. Which is what is tested
    // by this test.
    llvm.call @callee_(%x_arg, %x_arg) : (!llvm.ptr, !llvm.ptr) -> ()
    omp.terminator
  }
  llvm.return
}

llvm.func @callee_(%arg0: !llvm.ptr, %arg1: !llvm.ptr) {
  llvm.return
}

// CHECK: @0 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @1 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK-LABEL: define void @caller_(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca float, i64 1, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, i64 1, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_5:.*]] = load float, ptr %[[VAL_2]], align 4
// CHECK:   store float %[[VAL_5]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_6:.*]] = load i64, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_7:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   store i32 %[[VAL_7]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_8:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_caller__l{{.*}}(i64 %[[VAL_6]], i64 %[[VAL_8]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @callee_(
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_caller__l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_3:.*]], ptr %[[VAL_2]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_7:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_8:.*]] = sitofp i32 %[[VAL_7]] to float
// CHECK:   store float %[[VAL_8]], ptr %[[VAL_0]], align 4
// CHECK:   call void @callee_(ptr %[[VAL_0]], ptr %[[VAL_0]])
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
