// Test code-gen for `omp.parallel` ops with delayed privatizers (i.e. using
// `omp.private` ops).

// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s

llvm.func @parallel_op_1_private(%arg0: !llvm.ptr) {
  omp.parallel private(@x.privatizer %arg0 -> %arg2 : !llvm.ptr) {
    %0 = llvm.load %arg2 : !llvm.ptr -> f32
    omp.terminator
  }
  llvm.return
}



// Check that the privatizer alloc region was inlined properly.

// Check that the privatized value is used (rather than the original one).

llvm.func @parallel_op_2_privates(%arg0: !llvm.ptr, %arg1: !llvm.ptr) {
  omp.parallel private(@x.privatizer %arg0 -> %arg2, @y.privatizer %arg1 -> %arg3 : !llvm.ptr, !llvm.ptr) {
    %0 = llvm.load %arg2 : !llvm.ptr -> f32
    %1 = llvm.load %arg3 : !llvm.ptr -> i32
    omp.terminator
  }
  llvm.return
}



// Check that the privatizer alloc region was inlined properly.

// Check that the privatized value is used (rather than the original one).

omp.private {type = private} @x.privatizer : f32 init {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %c1 = llvm.mlir.constant(1 : i32) : i32
  %1 = llvm.load %arg0 : !llvm.ptr -> f32
  llvm.store %1, %arg1 : f32, !llvm.ptr
  omp.yield(%arg1 : !llvm.ptr)
}

omp.private {type = private} @y.privatizer : i32 init {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %c1 = llvm.mlir.constant(1 : i32) : i32
  %1 = llvm.load %arg0 : !llvm.ptr -> i32
  llvm.store %1, %arg1 : i32, !llvm.ptr
  omp.yield(%arg1 : !llvm.ptr)
}

// -----

llvm.func @parallel_op_private_multi_block(%arg0: !llvm.ptr) {
  omp.parallel private(@multi_block.privatizer %arg0 -> %arg2 : !llvm.ptr) {
    %0 = llvm.load %arg2 : !llvm.ptr -> f32
    omp.terminator
  }
  llvm.return
}




// Check contents of the first block in the `init` region.

// Check contents of the second block in the `init` region.

// Check that the privatizer's continuation block yileds the private clone's
// address.

// Check that the body of the parallel region loads from the private clone.

omp.private {type = private} @multi_block.privatizer : f32 init {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  llvm.br ^bb1(%arg0, %arg1 : !llvm.ptr, !llvm.ptr)

^bb1(%arg2: !llvm.ptr, %arg3: !llvm.ptr):
  %1 = llvm.load %arg2 : !llvm.ptr -> f32
  llvm.store %1, %arg3 : f32, !llvm.ptr
  omp.yield(%arg3 : !llvm.ptr)
}

// Tests fix for Fujitsu test suite test: 0007_0019.f90: the
// `llvm.mlir.addressof` op needs access to the parent module when lowering
// from the LLVM dialect to LLVM IR. If such op is used inside an `omp.private`
// op instance that was not created/cloned inside the module, we would get a
// seg fault due to trying to access a null pointer.

llvm.func @lower_region_with_addressof() {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x f64 {bindc_name = "u1"} : (i64) -> !llvm.ptr
  omp.parallel private(@_QFlower_region_with_addressof_privatizer %1 -> %arg0 : !llvm.ptr) {
    %c0 = llvm.mlir.constant(111 : i64) : i64
    %2 = llvm.getelementptr %arg0[%c0] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.call @bar(%2) : (!llvm.ptr) -> ()

    %c1 = llvm.mlir.constant(222 : i64) : i64
    %3 = llvm.mlir.addressof @_QQfoo: !llvm.ptr
    %4 = llvm.getelementptr %3[%c1] : (!llvm.ptr, i64) -> !llvm.ptr, f64
    llvm.call @bar(%4) : (!llvm.ptr) -> ()
    omp.terminator
  }

  llvm.return
}

omp.private {type = private} @_QFlower_region_with_addressof_privatizer : !llvm.ptr init {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %0 = llvm.mlir.addressof @_QQfoo: !llvm.ptr
  omp.yield(%0 : !llvm.ptr)
}

llvm.mlir.global linkonce constant @_QQfoo() {addr_space = 0 : i32} : !llvm.array<3 x i8> {
  %0 = llvm.mlir.constant("foo") : !llvm.array<3 x i8>
  llvm.return %0 : !llvm.array<3 x i8>
}

llvm.func @bar(!llvm.ptr)


// Tests fix for Fujitsu test suite test: 0275_0032.f90. The MLIR to LLVM
// translation logic assumed that reduction arguments to an `omp.parallel`
// op are always the last set of arguments to the op. However, this is a
// wrong assumption since private args come afterward. This tests the fix
// that we access the different sets of args properly.



llvm.func @private_and_reduction_() attributes {fir.internal_name = "_QPprivate_and_reduction", frame_pointer = #llvm.framePointerKind<all>, target_cpu = "x86-64"} {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8, array<1 x array<3 x i64>>)> : (i64) -> !llvm.ptr
  %2 = llvm.alloca %0 x f32 {bindc_name = "to_priv"} : (i64) -> !llvm.ptr
  omp.parallel private(@privatizer.part %2 -> %arg1 : !llvm.ptr) reduction(byref @reducer.part %1 -> %arg0 : !llvm.ptr) {
    %3 = llvm.load %arg0 : !llvm.ptr -> !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8, array<1 x array<3 x i64>>)>
    %4 = llvm.mlir.constant(8.000000e+00 : f32) : f32
    llvm.store %4, %arg1 : f32, !llvm.ptr
    omp.terminator
  }
  llvm.return
}

omp.private {type = private} @privatizer.part : f32

omp.declare_reduction @reducer.part : !llvm.ptr alloc {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8, array<1 x array<3 x i64>>)> : (i64) -> !llvm.ptr
  omp.yield(%1 : !llvm.ptr)
} init {
^bb0(%mold: !llvm.ptr, %alloc: !llvm.ptr):
  omp.yield(%alloc : !llvm.ptr)
} combiner {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  omp.yield(%arg0 : !llvm.ptr)
}  cleanup {
^bb0(%arg0: !llvm.ptr):
  omp.yield
}

// -----

llvm.func @_QPequivalence() {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x !llvm.array<4 x i8> : (i64) -> !llvm.ptr
  %2 = llvm.mlir.constant(0 : index) : i64
  %3 = llvm.getelementptr %1[0, %2] : (!llvm.ptr, i64) -> !llvm.ptr, !llvm.array<4 x i8>
  omp.parallel private(@_QFequivalenceEx_firstprivate_ptr_f32 %3 -> %arg0 : !llvm.ptr) {
    %4 = llvm.mlir.constant(3.140000e+00 : f32) : f32
    llvm.store %4, %arg0 : f32, !llvm.ptr
    omp.terminator
  }
  llvm.return
}

omp.private {type = firstprivate} @_QFequivalenceEx_firstprivate_ptr_f32 : f32 copy {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %0 = llvm.load %arg0 : !llvm.ptr -> f32
  llvm.store %0, %arg1 : f32, !llvm.ptr
  omp.yield(%arg1 : !llvm.ptr)
}

// Test that we initialize the firstprivate variable.
// Test that we inlined the body of the parallel region.

// -----

omp.private {type = private} @_QFEi_private_i32 : i32
llvm.func @_QPprivate_alloc_with_switch() {
  %0 = llvm.mlir.constant(1 : i32) : i32
  %1 = llvm.mlir.constant(30 : i32) : i32
  %2 = llvm.mlir.constant(1 : i64) : i64
  %3 = llvm.alloca %2 x i32 {bindc_name = "n"} : (i64) -> !llvm.ptr
  %4 = llvm.alloca %2 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
  llvm.store %0, %3 : i32, !llvm.ptr
  %5 = llvm.load %3 : !llvm.ptr -> i32
  llvm.switch %5 : i32, ^bb1 [
    1: ^bb1,
    2: ^bb2
  ]
^bb1:  // 2 preds: ^bb0, ^bb0
  omp.simd private(@_QFEi_private_i32 %4 -> %arg0 : !llvm.ptr) {
    omp.loop_nest (%arg1) : i32 = (%0) to (%1) inclusive step (%0) {
      llvm.store %arg1, %arg0 : i32, !llvm.ptr
      omp.yield
    }
  }
  llvm.br ^bb2
^bb2:  // 2 preds: ^bb0, ^bb1
  llvm.return
}

// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @parallel_op_1_private(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp_parallel:
// CHECK:   %[[VAL_4:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_5:.*]], ptr %[[VAL_4]], align 8
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @parallel_op_1_private..omp_par, ptr %[[VAL_0]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @parallel_op_1_private..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_1:.*]], i32 0, i32 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8, !align !1
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = load i32, ptr %[[VAL_5:.*]], align 4
// CHECK:   store i32 %[[VAL_4]], ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_7:.*]] = alloca float, align 4
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.private.init:
// CHECK:   %[[VAL_11:.*]] = load float, ptr %[[VAL_2]], align 4
// CHECK:   store float %[[VAL_11]], ptr %[[VAL_7]], align 4
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_13:.*]] = load float, ptr %[[VAL_7]], align 4
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @parallel_op_2_privates(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr, ptr }, align 8
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp_parallel:
// CHECK:   %[[VAL_4:.*]] = getelementptr {{[{]}} ptr, ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_5:.*]], ptr %[[VAL_4]], align 8
// CHECK:   %[[VAL_6:.*]] = getelementptr {{[{]}} ptr, ptr }, ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   store ptr %[[VAL_7:.*]], ptr %[[VAL_6]], align 8
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @parallel_op_2_privates..omp_par, ptr %[[VAL_0]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @parallel_op_2_privates..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr, ptr }, ptr %[[VAL_1:.*]], i32 0, i32 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8, !align !1
// CHECK:   %[[VAL_3:.*]] = getelementptr {{[{]}} ptr, ptr }, ptr %[[VAL_1]], i32 0, i32 1
// CHECK:   %[[VAL_4:.*]] = load ptr, ptr %[[VAL_3]], align 8, !align !1
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_7:.*]], align 4
// CHECK:   store i32 %[[VAL_6]], ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_9:.*]] = alloca float, align 4
// CHECK:   %[[VAL_10:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.private.init:
// CHECK:   %[[VAL_14:.*]] = load float, ptr %[[VAL_2]], align 4
// CHECK:   store float %[[VAL_14]], ptr %[[VAL_9]], align 4
// CHECK:   %[[VAL_15:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   store i32 %[[VAL_15]], ptr %[[VAL_10]], align 4
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.par.region2:
// CHECK:   %[[VAL_17:.*]] = load float, ptr %[[VAL_9]], align 4
// CHECK:   %[[VAL_18:.*]] = load i32, ptr %[[VAL_10]], align 4
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_22:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @_QQfoo = linkonce constant [3 x i8] c"foo"
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @.gomp_critical_user_.reduction.var = common global [8 x i32] zeroinitializer, align 8
// CHECK-LABEL: define void @parallel_op_private_multi_block(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp_parallel:
// CHECK:   %[[VAL_4:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_5:.*]], ptr %[[VAL_4]], align 8
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @parallel_op_private_multi_block..omp_par, ptr %[[VAL_0]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @parallel_op_private_multi_block..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_1:.*]], i32 0, i32 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8, !align !1
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = load i32, ptr %[[VAL_5:.*]], align 4
// CHECK:   store i32 %[[VAL_4]], ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_7:.*]] = alloca float, align 4
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.init1:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.private.init2:
// CHECK:   %[[VAL_13:.*]] = phi ptr [ %[[VAL_2]], %[[VAL_11]] ]
// CHECK:   %[[VAL_14:.*]] = phi ptr [ %[[VAL_7]], %[[VAL_11]] ]
// CHECK:   %[[VAL_15:.*]] = load float, ptr %[[VAL_13]], align 4
// CHECK:   store float %[[VAL_15]], ptr %[[VAL_14]], align 4
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.region.cont:
// CHECK:   %[[VAL_17:.*]] = phi ptr [ %[[VAL_14]], %[[VAL_12]] ]
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp.par.region4:
// CHECK:   %[[VAL_19:.*]] = load float, ptr %[[VAL_17]], align 4
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_22:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @lower_region_with_addressof(
// CHECK:   %[[VAL_0:.*]] = alloca double, i64 1, align 8
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @lower_region_with_addressof..omp_par)
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @lower_region_with_addressof..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca ptr, align 8
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.par.region1:
// CHECK:   call void @bar(ptr getelementptr (i8, ptr @_QQfoo, i64 888))
// CHECK:   call void @bar(ptr getelementptr (i8, ptr @_QQfoo, i64 1776))
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @private_and_reduction_(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8, [1 x [3 x i64]] }, i64 1, align 8
// CHECK:   %[[VAL_1:.*]] = alloca float, i64 1, align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @private_and_reduction_..omp_par)
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @private_and_reduction_..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca float, align 4
// CHECK:   %[[VAL_5:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8, [1 x [3 x i64]] }, i64 1, align 8
// CHECK:   %[[VAL_6:.*]] = alloca ptr, align 8
// CHECK:   %[[VAL_7:.*]] = alloca [1 x ptr], align 8
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.reduction.init:
// CHECK:   store ptr %[[VAL_5]], ptr %[[VAL_6]], align 8
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_13:.*]] = load {{[{]}} ptr, i64, i32, i8, i8, i8, i8, [1 x [3 x i64]] }, ptr %[[VAL_5]], align 8
// CHECK:   store float 8.000000e+00, ptr %[[VAL_4]], align 4
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.region.cont:
// CHECK:   %[[VAL_15:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_7]], i64 0, i64 0
// CHECK:   store ptr %[[VAL_6]], ptr %[[VAL_15]], align 8
// CHECK:   %[[VAL_16:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_17:.*]] = call i32 @__kmpc_reduce(ptr @1, i32 %[[VAL_16]], i32 1, i64 8, ptr %[[VAL_7]], ptr @.omp.reduction.func, ptr @.gomp_critical_user_.reduction.var)
// CHECK:   switch i32 %[[VAL_17]], label %[[VAL_18:.*]] [
// CHECK:   i32 1, label %[[VAL_19:.*]]
// CHECK:   i32 2, label %[[VAL_20:.*]]
// CHECK:   ]
// CHECK: reduce.switch.atomic:
// CHECK:   unreachable
// CHECK: reduce.switch.nonatomic:
// CHECK:   %[[VAL_21:.*]] = load ptr, ptr %[[VAL_6]], align 8
// CHECK:   call void @__kmpc_end_reduce(ptr @1, i32 %[[VAL_16]], ptr @.gomp_critical_user_.reduction.var)
// CHECK:   br label %[[VAL_18]]
// CHECK: reduce.finalize:
// CHECK:   br label %[[VAL_22:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: .fini:
// CHECK:   %[[VAL_24:.*]] = load ptr, ptr %[[VAL_6]], align 8
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @.omp.reduction.func(
// CHECK:   %[[VAL_0:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_1:.*]], i64 0, i64 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_3:.*]] = load ptr, ptr %[[VAL_2]], align 8
// CHECK:   %[[VAL_4:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_5:.*]], i64 0, i64 0
// CHECK:   %[[VAL_6:.*]] = load ptr, ptr %[[VAL_4]], align 8
// CHECK:   %[[VAL_7:.*]] = load ptr, ptr %[[VAL_6]], align 8
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @_QPequivalence(
// CHECK:   %[[VAL_0:.*]] = alloca [4 x i8], i64 1, align 1
// CHECK:   %[[VAL_1:.*]] = getelementptr [4 x i8], ptr %[[VAL_0]], i32 0, i64 0
// CHECK:   %[[VAL_2:.*]] = load float, ptr %[[VAL_1]], align 4
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_4:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @_QPequivalence..omp_par, float %[[VAL_2]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @_QPequivalence..omp_par(
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
// CHECK-LABEL: define void @_QPprivate_alloc_with_switch(
// CHECK:   %[[VAL_0:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, i64 1, align 4
// CHECK:   store i32 1, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_2:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   switch i32 %[[VAL_2]], label %[[VAL_5:.*]] [
// CHECK:   i32 1, label %[[VAL_5]]
// CHECK:   i32 2, label %[[VAL_6:.*]]
// CHECK:   ]
// CHECK: 4:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.simd.region:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_11:.*]] = phi i32 [ 0, %[[VAL_9]] ], [ %[[VAL_12:.*]], %[[VAL_13:.*]] ]
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_15:.*]] = icmp ult i32 %[[VAL_11]], 30
// CHECK:   br i1 %[[VAL_15]], label %[[VAL_16:.*]], label %[[VAL_17:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_18:.*]] = mul i32 %[[VAL_11]], 1
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_19]], ptr %[[VAL_3]], align 4, !llvm.access.group !1
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_13]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_12]] = add nuw i32 %[[VAL_11]], 1
// CHECK:   br label %[[VAL_10]], !llvm.loop !2
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_22:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_6]]
// CHECK: 7:
// CHECK:   ret void
// CHECK: }
