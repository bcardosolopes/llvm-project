// Test code-gen for `omp.parallel` ops with delayed privatizers (i.e. using
// `omp.private` ops).

// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s

llvm.func @parallel_op_firstprivate(%arg0: !llvm.ptr) {
  omp.parallel private(@x.privatizer %arg0 -> %arg2 : !llvm.ptr) {
    %0 = llvm.load %arg2 : !llvm.ptr -> f32
    omp.terminator
  }
  llvm.return
}

omp.private {type = firstprivate} @x.privatizer : f32 copy {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %0 = llvm.load %arg0 : !llvm.ptr -> f32
  llvm.store %0, %arg1 : f32, !llvm.ptr
  omp.yield(%arg1 : !llvm.ptr)
}



// Check that the privatizer alloc region was inlined properly.

// Check that the privatizer copy region was inlined properly.


// Check that the privatized value is used (rather than the original one).

// -----

llvm.func @parallel_op_firstprivate_multi_block(%arg0: !llvm.ptr) {
  omp.parallel private(@multi_block.privatizer %arg0 -> %arg2 : !llvm.ptr) {
    %0 = llvm.load %arg2 : !llvm.ptr -> f32
    omp.terminator
  }
  llvm.return
}



// Check that the body of the parallel region loads from the private clone.

// The 1st `alloc` block directly branches to the 2nd `alloc` block since the
// only insruction is `llvm.mlir.constant` which gets translated to compile-time
// constant in LLVM IR.





// Check that the init region still threads the private allocation through the
// multi-block control flow, while the copy step uses the by-value capture.

omp.private {type = firstprivate} @multi_block.privatizer : f32 init {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  llvm.br ^bb1

^bb1:
  omp.yield(%arg1 : !llvm.ptr)

} copy {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %0 = llvm.load %arg0 : !llvm.ptr -> f32
  llvm.br ^bb1(%0, %arg1 : f32, !llvm.ptr)

^bb1(%arg2: f32, %arg3: !llvm.ptr):
  llvm.store %arg2, %arg3 : f32, !llvm.ptr
  omp.yield(%arg3 : !llvm.ptr)
}

// -----

// Verifies fix for https://github.com/llvm/llvm-project/issues/102935.
//
// The issue happens since we previously failed to match MLIR values to their
// corresponding LLVM values in some cases (e.g. char strings with non-const
// length).
llvm.func @non_const_len_char_test(%n: !llvm.ptr {fir.bindc_name = "n"}) {
  %n_val = llvm.load %n : !llvm.ptr -> i64
  %orig_alloc = llvm.alloca %n_val x i8 {bindc_name = "str"} : (i64) -> !llvm.ptr
  %orig_val = llvm.mlir.undef : !llvm.struct<(ptr, i64)>
  %orig_val_init = llvm.insertvalue %orig_alloc, %orig_val[0] : !llvm.struct<(ptr, i64)>
  omp.parallel private(@non_const_len_char %orig_val_init -> %priv_arg : !llvm.struct<(ptr, i64)>) {
    %dummy = llvm.extractvalue %priv_arg[0] : !llvm.struct<(ptr, i64)>
    omp.terminator
  }
  llvm.return
}

omp.private {type = firstprivate} @non_const_len_char : !llvm.struct<(ptr, i64)> init {
^bb0(%orig_val: !llvm.struct<(ptr, i64)>, %arg1: !llvm.struct<(ptr, i64)>):
  %str_len = llvm.extractvalue %orig_val[1] : !llvm.struct<(ptr, i64)>
  %priv_alloc = llvm.alloca %str_len x i8 {bindc_name = "str", pinned} : (i64) -> !llvm.ptr
  %priv_val = llvm.mlir.undef : !llvm.struct<(ptr, i64)>
  %priv_val_init = llvm.insertvalue %priv_alloc, %priv_val[0] : !llvm.struct<(ptr, i64)>
  omp.yield(%priv_val_init : !llvm.struct<(ptr, i64)>)
} copy {
^bb0(%orig_val: !llvm.struct<(ptr, i64)>, %priv_val: !llvm.struct<(ptr, i64)>):
  llvm.call @foo() : () -> ()
  omp.yield(%priv_val : !llvm.struct<(ptr, i64)>)
}

llvm.func @foo()

// Verify that we found the privatizer by checking that we properly inlined the
// bodies of the alloc and copy regions.

// -----

// Verifies fix for https://github.com/llvm/llvm-project/issues/102939.
//
// The issues occurs because the CodeExtractor component only collect inputs
// (to the parallel regions) that are defined in the same function in which the
// parallel regions is present. Howerver, this is problematic because if we are
// privatizing a global value (e.g. a `target` variable which is emitted as a
// global), then we miss finding that input and we do not privatize the
// variable.

omp.private {type = firstprivate} @global_privatizer : f32 copy {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %0 = llvm.load %arg0 : !llvm.ptr -> f32
  llvm.store %0, %arg1 : f32, !llvm.ptr
  omp.yield(%arg1 : !llvm.ptr)
}

llvm.func @global_accessor() {
  %global_addr = llvm.mlir.addressof @global : !llvm.ptr
  omp.parallel private(@global_privatizer %global_addr -> %arg0 : !llvm.ptr) {
    %1 = llvm.mlir.constant(3.140000e+00 : f32) : f32
    llvm.store %1, %arg0 : f32, !llvm.ptr
    omp.terminator
  }
  llvm.return
}

llvm.mlir.global internal @global() {addr_space = 0 : i32} : f32 {
  %0 = llvm.mlir.zero : f32
  llvm.return %0 : f32
}

// Verify that we found the privatizer by checking that we properly inlined the
// bodies of the alloc and copy regions.

// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @parallel_op_firstprivate(
// CHECK:   %[[VAL_0:.*]] = load float, ptr %[[VAL_1:.*]], align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @parallel_op_firstprivate..omp_par, float %[[VAL_0]])
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @parallel_op_firstprivate..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca float, align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.private.copy:
// CHECK:   store float %[[VAL_9:.*]], ptr %[[VAL_4]], align 4
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_11:.*]] = load float, ptr %[[VAL_4]], align 4
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @parallel_op_firstprivate_multi_block(
// CHECK:   %[[VAL_0:.*]] = load float, ptr %[[VAL_1:.*]], align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @parallel_op_firstprivate_multi_block..omp_par, float %[[VAL_0]])
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @parallel_op_firstprivate_multi_block..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca float, align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.private.init1:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.private.init2:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.region.cont:
// CHECK:   %[[VAL_11:.*]] = phi ptr [ %[[VAL_4]], %[[VAL_9]] ]
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.private.copy:
// CHECK:   store float %[[VAL_13:.*]], ptr %[[VAL_11]], align 4
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.par.region4:
// CHECK:   %[[VAL_15:.*]] = load float, ptr %[[VAL_11]], align 4
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @non_const_len_char_test(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca {{[{]}} ptr, i64 }, align 8
// CHECK:   %[[VAL_2:.*]] = load i64, ptr %[[VAL_3:.*]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca i8, i64 %[[VAL_2]], align 1
// CHECK:   %[[VAL_5:.*]] = insertvalue {{[{]}} ptr, i64 } undef, ptr %[[VAL_4]], 0
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   store {{[{]}} ptr, i64 } %[[VAL_5]], ptr %[[VAL_1]], align 8
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp_parallel:
// CHECK:   %[[VAL_9:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_1]], ptr %[[VAL_9]], align 8
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @non_const_len_char_test..omp_par, ptr %[[VAL_0]])
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @non_const_len_char_test..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_1:.*]], i32 0, i32 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8, !align !1
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = load i32, ptr %[[VAL_5:.*]], align 4
// CHECK:   store i32 %[[VAL_4]], ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_7:.*]] = load {{[{]}} ptr, i64 }, ptr %[[VAL_2]], align 8
// CHECK:   %[[VAL_8:.*]] = alloca {{[{]}} ptr, i64 }, align 8
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init:
// CHECK:   %[[VAL_12:.*]] = extractvalue {{[{]}} ptr, i64 } %[[VAL_7]], 1
// CHECK:   %[[VAL_13:.*]] = alloca i8, i64 %[[VAL_12]], align 1
// CHECK:   %[[VAL_14:.*]] = insertvalue {{[{]}} ptr, i64 } undef, ptr %[[VAL_13]], 0
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.private.copy:
// CHECK:   call void @foo()
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_17:.*]] = extractvalue {{[{]}} ptr, i64 } %[[VAL_14]], 0
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @global = internal global float 0.000000e+00
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @global_accessor(
// CHECK:   %[[VAL_0:.*]] = load float, ptr @global, align 4
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @global_accessor..omp_par, float %[[VAL_0]])
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @global_accessor..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca float, align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.private.copy:
// CHECK:   store float %[[VAL_9:.*]], ptr %[[VAL_4]], align 4
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region1:
// CHECK:   store float 0x40091EB860000000, ptr %[[VAL_4]], align 4
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
