// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

omp.private {type = private} @simple_var.privatizer : i32

llvm.func @target_map_single_private() attributes {fir.internal_name = "_QPtarget_map_single_private"} {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x i32 {bindc_name = "simple_var"} : (i64) -> !llvm.ptr
  %3 = llvm.alloca %0 x i32 {bindc_name = "a"} : (i64) -> !llvm.ptr
  %4 = llvm.mlir.constant(2 : i32) : i32
  llvm.store %4, %3 : i32, !llvm.ptr
  %5 = omp.map.info var_ptr(%3 : !llvm.ptr, i32) map_clauses(to) capture(ByRef) -> !llvm.ptr {name = "a"}
  omp.target map_entries(%5 -> %arg0 : !llvm.ptr) private(@simple_var.privatizer %1 -> %arg1 : !llvm.ptr) {
    %6 = llvm.mlir.constant(10 : i32) : i32
    %7 = llvm.load %arg0 : !llvm.ptr -> i32
    %8 = llvm.add %7, %6 : i32
    llvm.store %8, %arg1 : i32, !llvm.ptr
    omp.terminator
  }
  llvm.return
}

omp.private {type = private} @n.privatizer : f32

llvm.func @target_map_2_privates() attributes {fir.internal_name = "_QPtarget_map_2_privates"} {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x i32 {bindc_name = "simple_var"} : (i64) -> !llvm.ptr
  %3 = llvm.alloca %0 x f32 {bindc_name = "n"} : (i64) -> !llvm.ptr
  %5 = llvm.alloca %0 x i32 {bindc_name = "a"} : (i64) -> !llvm.ptr
  %6 = llvm.mlir.constant(2 : i32) : i32
  llvm.store %6, %5 : i32, !llvm.ptr
  %7 = omp.map.info var_ptr(%5 : !llvm.ptr, i32) map_clauses(to) capture(ByRef) -> !llvm.ptr {name = "a"}
  omp.target map_entries(%7 -> %arg0 : !llvm.ptr) private(@simple_var.privatizer %1 -> %arg1, @n.privatizer %3 -> %arg2 : !llvm.ptr, !llvm.ptr) {
    %8 = llvm.mlir.constant(1.100000e+01 : f32) : f32
    %9 = llvm.mlir.constant(10 : i32) : i32
    %10 = llvm.load %arg0 : !llvm.ptr -> i32
    %11 = llvm.add %10, %9 : i32
    llvm.store %11, %arg1 : i32, !llvm.ptr
    %12 = llvm.load %arg1 : !llvm.ptr -> i32
    %13 = llvm.sitofp %12 : i32 to f32
    %14 = llvm.fadd %13, %8  {fastmathFlags = #llvm.fastmath<contract>} : f32
    llvm.store %14, %arg2 : f32, !llvm.ptr
    omp.terminator
  }
  llvm.return
}


// An entirely artifical privatizer that is meant to check multi-block
// privatizers. The idea here is to prove that we set the correct
// insertion points for the builder when generating, first, LLVM IR for the
// privatizer and then for the actual target region.
omp.private {type = private} @multi_block.privatizer : f32 init {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  llvm.br ^bb1

^bb1:
  omp.yield(%arg1 : !llvm.ptr)
}

llvm.func @target_op_private_multi_block(%arg0: !llvm.ptr) {
  omp.target private(@multi_block.privatizer %arg0 -> %arg2 : !llvm.ptr) {
    %0 = llvm.load %arg2 : !llvm.ptr -> f32
    omp.terminator
  }
  llvm.return
}

// Descriptors are needed for CHARACTER arrays and their type is
// !fir.boxchar<KIND>. When such arrays are used in the private construct, the
// privatizer takes a !fir.boxchar<KIND> as input. This type is lowered to
// !llvm.struct<(ptr, i64)>. This is unique because with other types of data,
// typically, the privatizer funtion takes a !llvm.ptr. Now, on the host side,
// we map the descriptor using the map clause of the omp.target op. Map clauses
// take only !llvm.ptr types. This means, we have a case where the descriptor is
// mapped by its pointer whereas the privatizer function expects the descriptor
// by value. So, we have this test to ensure that the compiler correctly loads
// from the mapped pointer before passing that to the privatizer function.
omp.private {type = private} @_QFtarget_boxcharEchar_var_private_boxchar_c8xU : !llvm.struct<(ptr, i64)> init {
^bb0(%arg0: !llvm.struct<(ptr, i64)>, %arg1: !llvm.struct<(ptr, i64)>):
  %0 = llvm.extractvalue %arg0[0] : !llvm.struct<(ptr, i64)>
  %1 = llvm.extractvalue %arg0[1] : !llvm.struct<(ptr, i64)>
  %2 = llvm.mlir.constant(1 : i64) : i64
  %3 = llvm.alloca %1 x i8 {bindc_name = "char_var", pinned} : (i64) -> !llvm.ptr
  %4 = llvm.mlir.undef : !llvm.struct<(ptr, i64)>
  %5 = llvm.insertvalue %3, %4[0] : !llvm.struct<(ptr, i64)>
  %6 = llvm.insertvalue %1, %5[1] : !llvm.struct<(ptr, i64)>
  omp.yield(%6 : !llvm.struct<(ptr, i64)>)
}
llvm.func @target_boxchar_(%arg0: !llvm.ptr {fir.bindc_name = "l"}) attributes {fir.internal_name = "_QPtarget_boxchar"} {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x i32 {bindc_name = "mapped_var"} : (i64) -> !llvm.ptr
  %3 = llvm.alloca %0 x !llvm.struct<(ptr, i64)> : (i64) -> !llvm.ptr
  %4 = llvm.mlir.constant(0 : i64) : i64
  %5 = llvm.load %arg0 : !llvm.ptr -> i64
  %6 = llvm.icmp "sgt" %5, %4 : i64
  %7 = llvm.select %6, %5, %4 : i1, i64
  %9 = llvm.alloca %7 x i8 {bindc_name = "char_var"} : (i64) -> !llvm.ptr
  %10 = llvm.mlir.undef : !llvm.struct<(ptr, i64)>
  %11 = llvm.insertvalue %9, %10[0] : !llvm.struct<(ptr, i64)>
  %12 = llvm.insertvalue %7, %11[1] : !llvm.struct<(ptr, i64)>
  %13 = omp.map.info var_ptr(%1 : !llvm.ptr, i32) map_clauses(implicit, exit_release_or_enter_alloc) capture(ByCopy) -> !llvm.ptr {name = "mapped_var"}
  llvm.store %12, %3 : !llvm.struct<(ptr, i64)>, !llvm.ptr
  %14 = omp.map.info var_ptr(%3 : !llvm.ptr, !llvm.struct<(ptr, i64)>) map_clauses(to) capture(ByRef) -> !llvm.ptr
  omp.target map_entries(%13 -> %arg1, %14 -> %arg2 : !llvm.ptr, !llvm.ptr) private(@_QFtarget_boxcharEchar_var_private_boxchar_c8xU %12 -> %arg3 [map_idx=1] : !llvm.struct<(ptr, i64)>) {
    %15 = llvm.mlir.constant(0 : index) : i64
    %16 = llvm.mlir.constant(32 : i8) : i8
    %17 = llvm.mlir.constant(1 : index) : i64
    %18 = llvm.mlir.constant(false) : i1
    %19 = llvm.mlir.constant(5 : index) : i64
    %20 = llvm.mlir.constant(5 : i32) : i32
    %21 = llvm.extractvalue %arg3[0] : !llvm.struct<(ptr, i64)>
    %22 = llvm.extractvalue %arg3[1] : !llvm.struct<(ptr, i64)>
    llvm.store %20, %arg1 : i32, !llvm.ptr
    %23 = llvm.mlir.addressof @_QQclX68656C6C6F : !llvm.ptr
    %24 = llvm.icmp "slt" %22, %19 : i64
    %25 = llvm.select %24, %22, %19 : i1, i64
    llvm.call @llvm.memmove.p0.p0.i64(%21, %23, %25, %18) : (!llvm.ptr, !llvm.ptr, i64, i1) -> ()
    %26 = llvm.sub %22, %17 : i64
    %27 = llvm.mlir.undef : !llvm.array<1 x i8>
    %28 = llvm.insertvalue %16, %27[0] : !llvm.array<1 x i8>
    %29 = llvm.sub %26, %25 : i64
    %30 = llvm.add %29, %17 : i64
    llvm.br ^bb1(%25, %30 : i64, i64)
  ^bb1(%31: i64, %32: i64):  // 2 preds: ^bb0, ^bb2
    %33 = llvm.icmp "sgt" %32, %15 : i64
    llvm.cond_br %33, ^bb2, ^bb3
  ^bb2:  // pred: ^bb1
    %34 = llvm.getelementptr %21[%31] : (!llvm.ptr, i64) -> !llvm.ptr, !llvm.array<1 x i8>
    llvm.store %28, %34 : !llvm.array<1 x i8>, !llvm.ptr
    %35 = llvm.add %31, %17 : i64
    %36 = llvm.sub %32, %17 : i64
    llvm.br ^bb1(%35, %36 : i64, i64)
  ^bb3:  // pred: ^bb1
    omp.terminator
  }
  llvm.return
}
llvm.mlir.global linkonce constant @_QQclX68656C6C6F() comdat(@__llvm_comdat::@_QQclX68656C6C6F) {addr_space = 0 : i32} : !llvm.array<5 x i8> {
  %0 = llvm.mlir.constant("hello") : !llvm.array<5 x i8>
  llvm.return %0 : !llvm.array<5 x i8>
}
llvm.comdat @__llvm_comdat {
  llvm.comdat_selector @_QQclX68656C6C6F any
}
llvm.func @llvm.memmove.p0.p0.i64(!llvm.ptr, !llvm.ptr, i64, i1) attributes {sym_visibility = "private"}



omp.private {type = firstprivate} @sf.firstprivate : f32 copy {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %0 = llvm.load %arg0 : !llvm.ptr -> f32
  llvm.store %0, %arg1 : f32, !llvm.ptr
  omp.yield(%arg1 : !llvm.ptr)
}
omp.private {type = firstprivate} @sv.firstprivate : i32 copy {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %0 = llvm.load %arg0 : !llvm.ptr -> i32
  llvm.store %0, %arg1 : i32, !llvm.ptr
  omp.yield(%arg1 : !llvm.ptr)
}
llvm.func @target_firstprivate_() attributes {fir.internal_name = "_QPtarget_firstprivate"} {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %sv = llvm.alloca %0 x i32 {bindc_name = "sv"} : (i64) -> !llvm.ptr
  %sf = llvm.alloca %0 x f32 {bindc_name = "sf"} : (i64) -> !llvm.ptr
  %6 = omp.map.info var_ptr(%sv : !llvm.ptr, i32) map_clauses(to) capture(ByRef) -> !llvm.ptr
  %7 = omp.map.info var_ptr(%sf : !llvm.ptr, f32) map_clauses(to) capture(ByRef) -> !llvm.ptr
  omp.target map_entries(%6 -> %arg0, %7 -> %arg1 : !llvm.ptr, !llvm.ptr) private(@sv.firstprivate %sv -> %arg2 [map_idx=0], @sf.firstprivate %sf -> %arg3 [map_idx=1] : !llvm.ptr, !llvm.ptr) {
    %8 = llvm.mlir.constant(2.000000e+00 : f64) : f64
    %9 = llvm.mlir.constant(10 : i32) : i32
    %10 = llvm.load %arg2 : !llvm.ptr -> i32
    %11 = llvm.add %10, %9 : i32
    llvm.store %11, %arg2 : i32, !llvm.ptr
    %12 = llvm.load %arg3 : !llvm.ptr -> f32
    %13 = llvm.fpext %12 : f32 to f64
    %14 = llvm.fadd %13, %8 {fastmathFlags = #llvm.fastmath<contract>} : f64
    %15 = llvm.fptrunc %14 : f64 to f32
    llvm.store %15, %arg3 : f32, !llvm.ptr
    omp.terminator
  }
  llvm.return
}










// CHECK-NEXT  store float %[[INIT_SF]], ptr %[[SF_PRIV_ALLOCA]], align 4

// CHECK: $_QQclX68656C6C6F = comdat any
// CHECK: @_QQclX68656C6C6F = linkonce constant [5 x i8] c"hello", comdat
// CHECK: @0 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @1 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @2 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @3 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @4 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @5 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK-LABEL: define void @target_map_single_private(
// CHECK:   %[[VAL_0:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, i64 1, align 4
// CHECK:   store i32 2, ptr %[[VAL_1]], align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_target_map_single_private_l{{.*}}(ptr %[[VAL_1]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @target_map_2_privates(
// CHECK:   %[[VAL_0:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_1:.*]] = alloca float, i64 1, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, i64 1, align 4
// CHECK:   store i32 2, ptr %[[VAL_2]], align 4
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: entry:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_target_map_2_privates_l{{.*}}(ptr %[[VAL_2]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @target_op_private_multi_block(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_target_op_private_multi_block_l{{.*}}()
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @target_boxchar_(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_2:.*]] = alloca {{[{]}} ptr, i64 }, i64 1, align 8
// CHECK:   %[[VAL_3:.*]] = load i64, ptr %[[VAL_4:.*]], align 4
// CHECK:   %[[VAL_5:.*]] = icmp sgt i64 %[[VAL_3]], 0
// CHECK:   %[[VAL_6:.*]] = select i1 %[[VAL_5]], i64 %[[VAL_3]], i64 0
// CHECK:   %[[VAL_7:.*]] = alloca i8, i64 %[[VAL_6]], align 1
// CHECK:   %[[VAL_8:.*]] = insertvalue {{[{]}} ptr, i64 } undef, ptr %[[VAL_7]], 0
// CHECK:   %[[VAL_9:.*]] = insertvalue {{[{]}} ptr, i64 } %[[VAL_8]], i64 %[[VAL_6]], 1
// CHECK:   store {{[{]}} ptr, i64 } %[[VAL_9]], ptr %[[VAL_2]], align 8
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_11:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   store i32 %[[VAL_11]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_12:.*]] = load i64, ptr %[[VAL_0]], align 4
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_target_boxchar__l{{.*}}(i64 %[[VAL_12]], ptr %[[VAL_2]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @target_firstprivate_(
// CHECK:   %[[VAL_0:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_1:.*]] = alloca float, i64 1, align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_target_firstprivate__l{{.*}}(ptr %[[VAL_0]], ptr %[[VAL_1]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_target_map_single_private_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_5:.*]] = load i32, ptr %[[VAL_6:.*]], align 4
// CHECK:   %[[VAL_7:.*]] = add i32 %[[VAL_5]], 10
// CHECK:   store i32 %[[VAL_7]], ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_target_map_2_privates_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca float, align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_7:.*]], align 4
// CHECK:   %[[VAL_8:.*]] = add i32 %[[VAL_6]], 10
// CHECK:   store i32 %[[VAL_8]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_9:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_10:.*]] = sitofp i32 %[[VAL_9]] to float
// CHECK:   %[[VAL_11:.*]] = fadd contract float %[[VAL_10]], 1.100000e+01
// CHECK:   store float %[[VAL_11]], ptr %[[VAL_1]], align 4
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_target_op_private_multi_block_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca float, align 4
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.private.init2:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.private.init1:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.region.cont:
// CHECK:   %[[VAL_7:.*]] = phi ptr [ %[[VAL_0]], %[[VAL_6]] ]
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_9:.*]] = load float, ptr %[[VAL_7]], align 4
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.region.cont3:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_target_boxchar__l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_2:.*]] = alloca {{[{]}} ptr, i64 }, align 8
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.private.init:
// CHECK:   %[[VAL_6:.*]] = load {{[{]}} ptr, i64 }, ptr %[[VAL_7:.*]], align 8
// CHECK:   %[[VAL_8:.*]] = extractvalue {{[{]}} ptr, i64 } %[[VAL_6]], 0
// CHECK:   %[[VAL_9:.*]] = extractvalue {{[{]}} ptr, i64 } %[[VAL_6]], 1
// CHECK:   %[[VAL_10:.*]] = alloca i8, i64 %[[VAL_9]], align 1
// CHECK:   %[[VAL_11:.*]] = insertvalue {{[{]}} ptr, i64 } undef, ptr %[[VAL_10]], 0
// CHECK:   %[[VAL_12:.*]] = insertvalue {{[{]}} ptr, i64 } %[[VAL_11]], i64 %[[VAL_9]], 1
// CHECK:   %[[VAL_13:.*]] = load {{[{]}} ptr, i64 }, ptr %[[VAL_7]], align 8
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.target3:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.target2:
// CHECK:   %[[VAL_16:.*]] = getelementptr [1 x i8], ptr %[[VAL_17:.*]], i64 %[[VAL_18:.*]]
// CHECK:   store [1 x i8] c" ", ptr %[[VAL_16]], align 1
// CHECK:   %[[VAL_19:.*]] = add i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = sub i64 %[[VAL_21:.*]], 1
// CHECK:   br label %[[VAL_22:.*]]
// CHECK: omp.target1:
// CHECK:   %[[VAL_18]] = phi i64 [ %[[VAL_19]], %[[VAL_23:.*]] ], [ %[[VAL_24:.*]], %[[VAL_14]] ]
// CHECK:   %[[VAL_21]] = phi i64 [ %[[VAL_20]], %[[VAL_23]] ], [ %[[VAL_25:.*]], %[[VAL_14]] ]
// CHECK:   %[[VAL_26:.*]] = icmp sgt i64 %[[VAL_21]], 0
// CHECK:   br i1 %[[VAL_26]], label %[[VAL_23]], label %[[VAL_27:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_17]] = extractvalue {{[{]}} ptr, i64 } %[[VAL_12]], 0
// CHECK:   %[[VAL_28:.*]] = extractvalue {{[{]}} ptr, i64 } %[[VAL_12]], 1
// CHECK:   store i32 5, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_29:.*]] = icmp slt i64 %[[VAL_28]], 5
// CHECK:   %[[VAL_24]] = select i1 %[[VAL_29]], i64 %[[VAL_28]], i64 5
// CHECK:   call void @llvm.memmove.p0.p0.i64(ptr %[[VAL_17]], ptr @_QQclX68656C6C6F, i64 %[[VAL_24]], i1 false)
// CHECK:   %[[VAL_30:.*]] = sub i64 %[[VAL_28]], 1
// CHECK:   %[[VAL_31:.*]] = sub i64 %[[VAL_30]], %[[VAL_24]]
// CHECK:   %[[VAL_25]] = add i64 %[[VAL_31]], 1
// CHECK:   br label %[[VAL_22]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_target_firstprivate__l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca float, align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.private.copy:
// CHECK:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_7:.*]], align 4
// CHECK:   store i32 %[[VAL_6]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_8:.*]] = load float, ptr %[[VAL_9:.*]], align 4
// CHECK:   store float %[[VAL_8]], ptr %[[VAL_1]], align 4
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_11:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_12:.*]] = add i32 %[[VAL_11]], 10
// CHECK:   store i32 %[[VAL_12]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_13:.*]] = load float, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_14:.*]] = fpext float %[[VAL_13]] to double
// CHECK:   %[[VAL_15:.*]] = fadd contract double %[[VAL_14]], 2.000000e+00
// CHECK:   %[[VAL_16:.*]] = fptrunc double %[[VAL_15]] to float
// CHECK:   store float %[[VAL_16]], ptr %[[VAL_1]], align 4
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
