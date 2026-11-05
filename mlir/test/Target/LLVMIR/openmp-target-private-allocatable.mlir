// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

llvm.func @alloc_foo_1(!llvm.ptr)
llvm.func @dealloc_foo_1(!llvm.ptr)

omp.private {type = private} @box.heap_privatizer : !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> init {
^bb0(%arg0: !llvm.ptr, %arg1 : !llvm.ptr):
  llvm.call @alloc_foo_1(%arg0) : (!llvm.ptr) -> ()
  omp.yield(%arg1 : !llvm.ptr)
} dealloc {
// There is no reason for the dealloc region here to have two blocks.
// But a multi-block test is useful in checking that the OMPIRBuilder
// has updated the InsertPoint properly after translating the dealloc
// region to LLVMIR.
// See https://github.com/llvm/llvm-project/issues/129202 for more
// context
^bb0(%arg0: !llvm.ptr):
  llvm.br ^bb1
^bb1:
  llvm.call @dealloc_foo_1(%arg0) : (!llvm.ptr) -> ()
  omp.yield
}

llvm.func @target_allocatable_(%arg0: !llvm.ptr {fir.bindc_name = "lb"}, %arg1: !llvm.ptr {fir.bindc_name = "ub"}, %arg2: !llvm.ptr {fir.bindc_name = "l"}) attributes {fir.internal_name = "_QPtarget_allocatable"} {
  %0 = llvm.mlir.constant(1 : i32) : i32
  %1 = llvm.alloca %0 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %3 = llvm.alloca %0 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %4 = llvm.mlir.constant(1 : i64) : i64
  %5 = llvm.alloca %4 x f32 {bindc_name = "real_var"} : (i64) -> !llvm.ptr
  %7 = llvm.alloca %4 x i32 {bindc_name = "mapped_var"} : (i64) -> !llvm.ptr
  %9 = llvm.alloca %4 x !llvm.struct<(f32, f32)> {bindc_name = "comp_var"} : (i64) -> !llvm.ptr
  %11 = llvm.alloca %0 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> {alignment = 8 : i64} : (i32) -> !llvm.ptr
  %13 = llvm.alloca %4 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> {bindc_name = "alloc_var"} : (i64) -> !llvm.ptr
  %39 = llvm.load %arg2 : !llvm.ptr -> i64
  %52 = llvm.alloca %39 x f32 {bindc_name = "real_arr"} : (i64) -> !llvm.ptr
  %53 = omp.map.info var_ptr(%7 : !llvm.ptr, i32) map_clauses(implicit, exit_release_or_enter_alloc) capture(ByCopy) -> !llvm.ptr {name = "mapped_var"}
  %54 = omp.map.info var_ptr(%13 : !llvm.ptr, !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)>) map_clauses(to) capture(ByRef) -> !llvm.ptr
  omp.target map_entries(%53 -> %arg3, %54 -> %arg4 : !llvm.ptr, !llvm.ptr) private(@box.heap_privatizer %13 -> %arg5 [map_idx=1] : !llvm.ptr) {
    llvm.call @use_private_var(%arg5) : (!llvm.ptr) -> ()
    omp.terminator
  }
  llvm.return
}

llvm.func @use_private_var(!llvm.ptr) -> ()

llvm.func @_FortranAAssign(!llvm.ptr, !llvm.ptr, !llvm.ptr, i32) -> !llvm.struct<()> attributes {fir.runtime, sym_visibility = "private"}

// The first set of checks ensure that we are calling the offloaded function
// with the right arguments, especially the second argument which needs to
// be a memory reference to the descriptor for the privatized allocatable

// The second set of checks ensure that to allocate memory for the
// allocatable, we are, in fact, using the memory reference of the descriptor
// passed as the second argument to the offloaded function.



// Now, check the deallocation of the private var.

// CHECK: @0 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @1 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK-LABEL: define void @target_allocatable_(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, align 8
// CHECK:   %[[VAL_2:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, align 8
// CHECK:   %[[VAL_3:.*]] = alloca float, i64 1, align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_5:.*]] = alloca {{[{]}} float, float }, i64 1, align 8
// CHECK:   %[[VAL_6:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, align 8
// CHECK:   %[[VAL_7:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, i64 1, align 8
// CHECK:   %[[VAL_8:.*]] = load i64, ptr %[[VAL_9:.*]], align 4
// CHECK:   %[[VAL_10:.*]] = alloca float, i64 %[[VAL_8]], align 4
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_12:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   store i32 %[[VAL_12]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_13:.*]] = load i64, ptr %[[VAL_0]], align 4
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_target_allocatable__l{{.*}}(i64 %[[VAL_13]], ptr %[[VAL_7]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_target_allocatable__l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_2:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, align 8
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.private.init:
// CHECK:   call void @alloc_foo_1(ptr %[[VAL_6:.*]])
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.target:
// CHECK:   call void @use_private_var(ptr %[[VAL_2]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.targetop.private.cleanup2:
// CHECK:   call void @dealloc_foo_1(ptr %[[VAL_2]])
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.targetop.private.cleanup:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.cont1:
// CHECK:   ret void
// CHECK: }
