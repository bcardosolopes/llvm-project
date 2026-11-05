// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

omp.declare_reduction @add_reduction_f32 : f32 init {
^bb0(%arg0: f32):
  %0 = llvm.mlir.constant(0.000000e+00 : f32) : f32
  omp.yield(%0 : f32)
} combiner {
^bb0(%arg0: f32, %arg1: f32):
  %0 = llvm.fadd %arg0, %arg1  {fastmathFlags = #llvm.fastmath<contract>} : f32
  omp.yield(%0 : f32)
}  
llvm.func @sections_(%arg0: !llvm.ptr {fir.bindc_name = "x"}) attributes {fir.internal_name = "_QPsections"} {
  %0 = llvm.mlir.constant(2.000000e+00 : f32) : f32
  %1 = llvm.mlir.constant(1.000000e+00 : f32) : f32
  omp.parallel {
    omp.sections reduction(@add_reduction_f32 %arg0 -> %arg1 : !llvm.ptr) {
      omp.section {
      ^bb0(%arg2: !llvm.ptr):
        %2 = llvm.load %arg2 : !llvm.ptr -> f32
        %3 = llvm.fadd %2, %1  {fastmathFlags = #llvm.fastmath<contract>} : f32
        llvm.store %3, %arg2 : f32, !llvm.ptr
        omp.terminator
      }
      omp.section {
      ^bb0(%arg2: !llvm.ptr):
        %2 = llvm.load %arg2 : !llvm.ptr -> f32
        %3 = llvm.fadd %2, %0  {fastmathFlags = #llvm.fastmath<contract>} : f32
        llvm.store %3, %arg2 : f32, !llvm.ptr
        omp.terminator
      }
      omp.terminator
    }
    omp.terminator
  }
  llvm.return
}

// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @.gomp_critical_user_.reduction.var = common global [8 x i32] zeroinitializer, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @sections_(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp_parallel:
// CHECK:   %[[VAL_4:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_5:.*]], ptr %[[VAL_4]], align 8
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @sections_..omp_par, ptr %[[VAL_0]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @sections_..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_1:.*]], i32 0, i32 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8, !align !1
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_9:.*]], align 4
// CHECK:   store i32 %[[VAL_8]], ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_10:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_11:.*]] = alloca float, align 4
// CHECK:   %[[VAL_12:.*]] = alloca [1 x ptr], align 8
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.after_alloca2:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.par.region1:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.reduction.init:
// CHECK:   %[[VAL_18:.*]] = load float, ptr %[[VAL_2]], align 4
// CHECK:   store float 0.000000e+00, ptr %[[VAL_11]], align 4
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp_section_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_4]], align 4
// CHECK:   store i32 1, ptr %[[VAL_5]], align 4
// CHECK:   store i32 1, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_20:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_20]], i32 34, ptr %[[VAL_3]], ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], i32 1, i32 1)
// CHECK:   %[[VAL_21:.*]] = load i32, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_22:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_23:.*]] = sub i32 %[[VAL_22]], %[[VAL_21]]
// CHECK:   %[[VAL_24:.*]] = add i32 %[[VAL_23]], 1
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_section_loop.header:
// CHECK:   %[[VAL_26:.*]] = phi i32 [ 0, %[[VAL_19]] ], [ %[[VAL_27:.*]], %[[VAL_28:.*]] ]
// CHECK:   br label %[[VAL_29:.*]]
// CHECK: omp_section_loop.cond:
// CHECK:   %[[VAL_30:.*]] = icmp ult i32 %[[VAL_26]], %[[VAL_24]]
// CHECK:   br i1 %[[VAL_30]], label %[[VAL_31:.*]], label %[[VAL_32:.*]]
// CHECK: omp_section_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_20]])
// CHECK:   br label %[[VAL_33:.*]]
// CHECK: omp_section_loop.after:
// CHECK:   %[[VAL_34:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_12]], i64 0, i64 0
// CHECK:   store ptr %[[VAL_11]], ptr %[[VAL_34]], align 8
// CHECK:   %[[VAL_35:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_36:.*]] = call i32 @__kmpc_reduce(ptr @1, i32 %[[VAL_35]], i32 1, i64 8, ptr %[[VAL_12]], ptr @.omp.reduction.func, ptr @.gomp_critical_user_.reduction.var)
// CHECK:   switch i32 %[[VAL_36]], label %[[VAL_37:.*]] [
// CHECK:   i32 1, label %[[VAL_38:.*]]
// CHECK:   i32 2, label %[[VAL_39:.*]]
// CHECK:   ]
// CHECK: reduce.switch.atomic:
// CHECK:   unreachable
// CHECK: reduce.switch.nonatomic:
// CHECK:   %[[VAL_40:.*]] = load float, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_41:.*]] = load float, ptr %[[VAL_11]], align 4
// CHECK:   %[[VAL_42:.*]] = fadd contract float %[[VAL_40]], %[[VAL_41]]
// CHECK:   store float %[[VAL_42]], ptr %[[VAL_2]], align 4
// CHECK:   call void @__kmpc_end_reduce(ptr @1, i32 %[[VAL_35]], ptr @.gomp_critical_user_.reduction.var)
// CHECK:   br label %[[VAL_37]]
// CHECK: reduce.finalize:
// CHECK:   %[[VAL_43:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_43]])
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp_section_loop.body:
// CHECK:   %[[VAL_48:.*]] = add i32 %[[VAL_26]], %[[VAL_21]]
// CHECK:   %[[VAL_49:.*]] = mul i32 %[[VAL_48]], 1
// CHECK:   %[[VAL_50:.*]] = add i32 %[[VAL_49]], 0
// CHECK:   switch i32 %[[VAL_50]], label %[[VAL_51:.*]] [
// CHECK:   i32 0, label %[[VAL_52:.*]]
// CHECK:   i32 1, label %[[VAL_53:.*]]
// CHECK:   ]
// CHECK: omp_section_loop.body.case4:
// CHECK:   br label %[[VAL_54:.*]]
// CHECK: omp.section.region6:
// CHECK:   %[[VAL_55:.*]] = load float, ptr %[[VAL_11]], align 4
// CHECK:   %[[VAL_56:.*]] = fadd contract float %[[VAL_55]], 2.000000e+00
// CHECK:   store float %[[VAL_56]], ptr %[[VAL_11]], align 4
// CHECK:   br label %[[VAL_57:.*]]
// CHECK: omp.region.cont5:
// CHECK:   br label %[[VAL_51]]
// CHECK: omp_section_loop.body.case:
// CHECK:   br label %[[VAL_58:.*]]
// CHECK: omp.section.region:
// CHECK:   %[[VAL_59:.*]] = load float, ptr %[[VAL_11]], align 4
// CHECK:   %[[VAL_60:.*]] = fadd contract float %[[VAL_59]], 1.000000e+00
// CHECK:   store float %[[VAL_60]], ptr %[[VAL_11]], align 4
// CHECK:   br label %[[VAL_61:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_51]]
// CHECK: omp_section_loop.body.sections.after:
// CHECK:   br label %[[VAL_28]]
// CHECK: omp_section_loop.inc:
// CHECK:   %[[VAL_27]] = add nuw i32 %[[VAL_26]], 1
// CHECK:   br label %[[VAL_25]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @.omp.reduction.func(
// CHECK:   %[[VAL_0:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_1:.*]], i64 0, i64 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8
// CHECK:   %[[VAL_3:.*]] = load float, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_4:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_5:.*]], i64 0, i64 0
// CHECK:   %[[VAL_6:.*]] = load ptr, ptr %[[VAL_4]], align 8
// CHECK:   %[[VAL_7:.*]] = load float, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_8:.*]] = fadd contract float %[[VAL_3]], %[[VAL_7]]
// CHECK:   store float %[[VAL_8]], ptr %[[VAL_2]], align 4
// CHECK:   ret void
// CHECK: }
