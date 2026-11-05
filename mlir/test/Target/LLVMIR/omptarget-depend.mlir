// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

module attributes {omp.is_target_device = false, omp.target_triples = ["amdgcn-amd-amdhsa"]} {
  llvm.func @_QQmain() attributes {fir.bindc_name = "main"} {
    %0 = llvm.mlir.constant(39 : index) : i64
    %1 = llvm.mlir.constant(0 : index) : i64
    %2 = llvm.mlir.constant(1 : index) : i64
    %3 = llvm.mlir.constant(40 : index) : i64
    %4 = llvm.mlir.addressof @_QFEa : !llvm.ptr
    %5 = llvm.mlir.addressof @_QFEb : !llvm.ptr
    %6 = llvm.mlir.constant(1 : i64) : i64
    %7 = llvm.alloca %6 x i32 {bindc_name = "i"} : (i64) -> !llvm.ptr
    %8 = llvm.mlir.addressof @_QFEn : !llvm.ptr
    omp.task {
      %14 = llvm.mlir.constant(1 : i64) : i64
      %15 = llvm.alloca %14 x i32 {bindc_name = "i", pinned} : (i64) -> !llvm.ptr
      %16 = llvm.load %8 : !llvm.ptr -> i32
      %17 = llvm.sext %16 : i32 to i64
      %18 = llvm.trunc %2 : i64 to i32
      llvm.br ^bb1(%18, %17 : i32, i64)
    ^bb1(%19: i32, %20: i64):  // 2 preds: ^bb0, ^bb2
      %21 = llvm.icmp "sgt" %20, %1 : i64
      llvm.cond_br %21, ^bb2, ^bb3
    ^bb2:  // pred: ^bb1
      llvm.store %19, %15 : i32, !llvm.ptr
      %22 = llvm.load %15 : !llvm.ptr -> i32
      %23 = llvm.sext %22 : i32 to i64
      %24 = llvm.mlir.constant(1 : i64) : i64
      %25 = llvm.mlir.constant(0 : i64) : i64
      %26 = llvm.sub %23, %24 overflow<nsw> : i64
      %27 = llvm.mul %26, %24 overflow<nsw> : i64
      %28 = llvm.mul %27, %24 overflow<nsw> : i64
      %29 = llvm.add %28, %25 overflow<nsw> : i64
      %30 = llvm.mul %24, %3 overflow<nsw> : i64
      %31 = llvm.getelementptr %4[%29] : (!llvm.ptr, i64) -> !llvm.ptr, i32
      llvm.store %22, %31 : i32, !llvm.ptr
      %32 = llvm.load %15 : !llvm.ptr -> i32
      %33 = llvm.add %32, %18 : i32
      %34 = llvm.sub %20, %2 : i64
      llvm.br ^bb1(%33, %34 : i32, i64)
    ^bb3:  // pred: ^bb1
      llvm.store %19, %15 : i32, !llvm.ptr
      omp.terminator
    }
    %9 = omp.map.bounds lower_bound(%1 : i64) upper_bound(%0 : i64) extent(%3 : i64) stride(%2 : i64) start_idx(%2 : i64)
    %10 = omp.map.info var_ptr(%4 : !llvm.ptr, !llvm.array<40 x i32>) map_clauses(to) capture(ByRef) bounds(%9) -> !llvm.ptr {name = "a"}
    %11 = omp.map.info var_ptr(%5 : !llvm.ptr, !llvm.array<40 x i32>) map_clauses(from) capture(ByRef) bounds(%9) -> !llvm.ptr {name = "b"}
    %12 = omp.map.info var_ptr(%7 : !llvm.ptr, i32) map_clauses(implicit, exit_release_or_enter_alloc) capture(ByCopy) -> !llvm.ptr {name = "i"}
    %13 = omp.map.info var_ptr(%8 : !llvm.ptr, i32) map_clauses(implicit, exit_release_or_enter_alloc) capture(ByCopy) -> !llvm.ptr {name = "n"}
    omp.target depend(taskdependin -> %4 : !llvm.ptr) map_entries(%10 -> %arg0, %11 -> %arg1, %12 -> %arg2, %13 -> %arg3 : !llvm.ptr, !llvm.ptr, !llvm.ptr, !llvm.ptr) {
      %14 = llvm.mlir.constant(0 : index) : i64
      %15 = llvm.mlir.constant(10 : i32) : i32
      %16 = llvm.mlir.constant(1 : index) : i64
      %17 = llvm.mlir.constant(40 : index) : i64
      %18 = llvm.load %arg3 : !llvm.ptr -> i32
      %19 = llvm.sext %18 : i32 to i64
      %20 = llvm.trunc %16 : i64 to i32
      llvm.br ^bb1(%20, %19 : i32, i64)
    ^bb1(%21: i32, %22: i64):  // 2 preds: ^bb0, ^bb2
      %23 = llvm.icmp "sgt" %22, %14 : i64
      llvm.cond_br %23, ^bb2, ^bb3
    ^bb2:  // pred: ^bb1
      llvm.store %21, %arg2 : i32, !llvm.ptr
      %24 = llvm.load %arg2 : !llvm.ptr -> i32
      %25 = llvm.sext %24 : i32 to i64
      %26 = llvm.mlir.constant(1 : i64) : i64
      %27 = llvm.mlir.constant(0 : i64) : i64
      %28 = llvm.sub %25, %26 overflow<nsw> : i64
      %29 = llvm.mul %28, %26 overflow<nsw> : i64
      %30 = llvm.mul %29, %26 overflow<nsw> : i64
      %31 = llvm.add %30, %27 overflow<nsw> : i64
      %32 = llvm.mul %26, %17 overflow<nsw> : i64
      %33 = llvm.getelementptr %arg0[%31] : (!llvm.ptr, i64) -> !llvm.ptr, i32
      %34 = llvm.load %33 : !llvm.ptr -> i32
      %35 = llvm.add %34, %15 : i32
      %36 = llvm.mlir.constant(1 : i64) : i64
      %37 = llvm.mlir.constant(0 : i64) : i64
      %38 = llvm.sub %25, %36 overflow<nsw> : i64
      %39 = llvm.mul %38, %36 overflow<nsw> : i64
      %40 = llvm.mul %39, %36 overflow<nsw> : i64
      %41 = llvm.add %40, %37 overflow<nsw> : i64
      %42 = llvm.mul %36, %17 overflow<nsw> : i64
      %43 = llvm.getelementptr %arg1[%41] : (!llvm.ptr, i64) -> !llvm.ptr, i32
      llvm.store %35, %43 : i32, !llvm.ptr
      %44 = llvm.load %arg2 : !llvm.ptr -> i32
      %45 = llvm.add %44, %20 : i32
      %46 = llvm.sub %22, %16 : i64
      llvm.br ^bb1(%45, %46 : i32, i64)
    ^bb3:  // pred: ^bb1
      llvm.store %21, %arg2 : i32, !llvm.ptr
      omp.terminator
    }
    llvm.return
  }
  llvm.mlir.global internal @_QFEa() {addr_space = 0 : i32} : !llvm.array<40 x i32> {
    %0 = llvm.mlir.zero : !llvm.array<40 x i32>
    llvm.return %0 : !llvm.array<40 x i32>
  }
  llvm.mlir.global internal @_QFEb() {addr_space = 0 : i32} : !llvm.array<40 x i32> {
    %0 = llvm.mlir.zero : !llvm.array<40 x i32>
    llvm.return %0 : !llvm.array<40 x i32>
  }
  llvm.mlir.global internal @_QFEc() {addr_space = 0 : i32} : !llvm.array<40 x i32> {
    %0 = llvm.mlir.zero : !llvm.array<40 x i32>
    llvm.return %0 : !llvm.array<40 x i32>
  }
  llvm.mlir.global internal @_QFEn() {addr_space = 0 : i32} : i32 {
    %0 = llvm.mlir.constant(40 : i32) : i32
    llvm.return %0 : i32
  }
  llvm.func @_FortranAProgramStart(i32, !llvm.ptr, !llvm.ptr, !llvm.ptr) attributes {sym_visibility = "private"}
  llvm.func @_FortranAProgramEndStatement() attributes {sym_visibility = "private"}
  llvm.func @main(%arg0: i32, %arg1: !llvm.ptr, %arg2: !llvm.ptr) -> i32 {
    %0 = llvm.mlir.constant(0 : i32) : i32
    %1 = llvm.mlir.zero : !llvm.ptr
    llvm.call @_FortranAProgramStart(%arg0, %arg1, %arg2, %1) {fastmathFlags = #llvm.fastmath<contract>} : (i32, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> ()
    llvm.call @_QQmain() {fastmathFlags = #llvm.fastmath<contract>} : () -> ()
    llvm.call @_FortranAProgramEndStatement() {fastmathFlags = #llvm.fastmath<contract>} : () -> ()
    llvm.return %0 : i32
  }
}

// %strucArg holds pointers to shared data.

// CHECK: @_QFEa = internal global [40 x i32] zeroinitializer
// CHECK: @_QFEb = internal global [40 x i32] zeroinitializer
// CHECK: @_QFEc = internal global [40 x i32] zeroinitializer
// CHECK: @_QFEn = internal global i32 40
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @3 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @4 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @5 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @.__omp_offloading_{{.*}}_{{.*}}__QQmain_l{{.*}}.region_id = weak constant i8 0
// CHECK: @.offload_sizes = private unnamed_addr constant [4 x i64] [i64 160, i64 160, i64 4, i64 4]
// CHECK: @.offload_maptypes = private unnamed_addr constant [4 x i64] [i64 33, i64 34, i64 800, i64 800]
// CHECK: @.offload_mapnames = private constant [4 x ptr] [ptr @2, ptr @3, ptr @4, ptr @5]
// CHECK: @.offloading.entry_name = internal unnamed_addr constant [{{.*}} x i8] c"__omp_offloading_{{.*}}_{{.*}}__QQmain_l{{.*}}\00", section ".llvm.rodata.offloading", align 1
// CHECK: @.offloading.entry.__omp_offloading_{{.*}}_{{.*}}__QQmain_l{{.*}} = weak constant %struct.__tgt_offload_entry {{[{]}} i64 0, i16 1, i16 1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}__QQmain_l{{.*}}.region_id, ptr @.offloading.entry_name, i64 0, i64 0, ptr null }, section "llvm_offload_entries", align 8
// CHECK-LABEL: define void @_QQmain(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr, ptr, i64, i64 }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_3:.*]] = alloca [4 x ptr], align 8
// CHECK:   %[[VAL_4:.*]] = alloca [4 x ptr], align 8
// CHECK:   %[[VAL_5:.*]] = alloca [4 x ptr], align 8
// CHECK:   %[[VAL_6:.*]] = alloca [1 x %[[VAL_7:.*]]], align 8
// CHECK:   %[[VAL_8:.*]] = alloca i32, i64 1, align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_14:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_15:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_14]], i32 1, i64 40, i64 0, ptr @_QQmain..omp_par)
// CHECK:   %[[VAL_16:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_14]], ptr %[[VAL_15]])
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: task.exit:
// CHECK:   %[[VAL_18:.*]] = load i32, ptr %[[VAL_8]], align 4
// CHECK:   store i32 %[[VAL_18]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_19:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_20:.*]] = load i32, ptr @_QFEn, align 4
// CHECK:   store i32 %[[VAL_20]], ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_21:.*]] = load i64, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_22:.*]] = getelementptr inbounds [4 x ptr], ptr %[[VAL_3]], i32 0, i32 0
// CHECK:   store ptr @_QFEa, ptr %[[VAL_22]], align 8
// CHECK:   %[[VAL_23:.*]] = getelementptr inbounds [4 x ptr], ptr %[[VAL_4]], i32 0, i32 0
// CHECK:   store ptr @_QFEa, ptr %[[VAL_23]], align 8
// CHECK:   %[[VAL_24:.*]] = getelementptr inbounds [4 x ptr], ptr %[[VAL_5]], i64 0, i64 0
// CHECK:   store ptr null, ptr %[[VAL_24]], align 8
// CHECK:   %[[VAL_25:.*]] = getelementptr inbounds [4 x ptr], ptr %[[VAL_3]], i32 0, i32 1
// CHECK:   store ptr @_QFEb, ptr %[[VAL_25]], align 8
// CHECK:   %[[VAL_26:.*]] = getelementptr inbounds [4 x ptr], ptr %[[VAL_4]], i32 0, i32 1
// CHECK:   store ptr @_QFEb, ptr %[[VAL_26]], align 8
// CHECK:   %[[VAL_27:.*]] = getelementptr inbounds [4 x ptr], ptr %[[VAL_5]], i64 0, i64 1
// CHECK:   store ptr null, ptr %[[VAL_27]], align 8
// CHECK:   %[[VAL_28:.*]] = getelementptr inbounds [4 x ptr], ptr %[[VAL_3]], i32 0, i32 2
// CHECK:   store i64 %[[VAL_19]], ptr %[[VAL_28]], align 8
// CHECK:   %[[VAL_29:.*]] = getelementptr inbounds [4 x ptr], ptr %[[VAL_4]], i32 0, i32 2
// CHECK:   store i64 %[[VAL_19]], ptr %[[VAL_29]], align 8
// CHECK:   %[[VAL_30:.*]] = getelementptr inbounds [4 x ptr], ptr %[[VAL_5]], i64 0, i64 2
// CHECK:   store ptr null, ptr %[[VAL_30]], align 8
// CHECK:   %[[VAL_31:.*]] = getelementptr inbounds [4 x ptr], ptr %[[VAL_3]], i32 0, i32 3
// CHECK:   store i64 %[[VAL_21]], ptr %[[VAL_31]], align 8
// CHECK:   %[[VAL_32:.*]] = getelementptr inbounds [4 x ptr], ptr %[[VAL_4]], i32 0, i32 3
// CHECK:   store i64 %[[VAL_21]], ptr %[[VAL_32]], align 8
// CHECK:   %[[VAL_33:.*]] = getelementptr inbounds [4 x ptr], ptr %[[VAL_5]], i64 0, i64 3
// CHECK:   store ptr null, ptr %[[VAL_33]], align 8
// CHECK:   %[[VAL_34:.*]] = getelementptr inbounds [4 x ptr], ptr %[[VAL_3]], i32 0, i32 0
// CHECK:   %[[VAL_35:.*]] = getelementptr inbounds [4 x ptr], ptr %[[VAL_4]], i32 0, i32 0
// CHECK:   br label %[[VAL_36:.*]]
// CHECK: codeRepl7:
// CHECK:   %[[VAL_37:.*]] = getelementptr {{[{]}} ptr, ptr, i64, i64 }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_34]], ptr %[[VAL_37]], align 8
// CHECK:   %[[VAL_38:.*]] = getelementptr {{[{]}} ptr, ptr, i64, i64 }, ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   store ptr %[[VAL_35]], ptr %[[VAL_38]], align 8
// CHECK:   %[[VAL_39:.*]] = getelementptr {{[{]}} ptr, ptr, i64, i64 }, ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   store i64 %[[VAL_19]], ptr %[[VAL_39]], align 4
// CHECK:   %[[VAL_40:.*]] = getelementptr {{[{]}} ptr, ptr, i64, i64 }, ptr %[[VAL_0]], i32 0, i32 3
// CHECK:   store i64 %[[VAL_21]], ptr %[[VAL_40]], align 4
// CHECK:   %[[VAL_41:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_42:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_41]], i32 0, i64 40, i64 32, ptr @.omp_target_task_proxy_func)
// CHECK:   %[[VAL_43:.*]] = getelementptr inbounds nuw %[[VAL_44:.*]], ptr %[[VAL_42]], i32 0, i32 0
// CHECK:   %[[VAL_45:.*]] = load ptr, ptr %[[VAL_43]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_45]], ptr align 1 %[[VAL_0]], i64 32, i1 false)
// CHECK:   %[[VAL_46:.*]] = getelementptr inbounds [1 x %[[VAL_7]]], ptr %[[VAL_6]], i64 0, i64 0
// CHECK:   %[[VAL_47:.*]] = getelementptr inbounds nuw %[[VAL_7]], ptr %[[VAL_46]], i32 0, i32 0
// CHECK:   store i64 ptrtoint (ptr @_QFEa to i64), ptr %[[VAL_47]], align 4
// CHECK:   %[[VAL_48:.*]] = getelementptr inbounds nuw %[[VAL_7]], ptr %[[VAL_46]], i32 0, i32 1
// CHECK:   store i64 160, ptr %[[VAL_48]], align 4
// CHECK:   %[[VAL_49:.*]] = getelementptr inbounds nuw %[[VAL_7]], ptr %[[VAL_46]], i32 0, i32 2
// CHECK:   store i8 1, ptr %[[VAL_49]], align 1
// CHECK:   call void @__kmpc_omp_taskwait_deps_51(ptr @1, i32 %[[VAL_41]], i32 1, ptr %[[VAL_6]], i32 0, ptr null, i32 0)
// CHECK:   call void @__kmpc_omp_task_begin_if0(ptr @1, i32 %[[VAL_41]], ptr %[[VAL_42]])
// CHECK:   call void @.omp_target_task_proxy_func(i32 %[[VAL_41]], ptr %[[VAL_42]])
// CHECK:   call void @__kmpc_omp_task_complete_if0(ptr @1, i32 %[[VAL_41]], ptr %[[VAL_42]])
// CHECK:   br label %[[VAL_50:.*]]
// CHECK: target.task.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @_QQmain..omp_par.1(
// CHECK: target.task.alloca:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr, ptr, i64, i64 }, ptr %[[VAL_1:.*]], i32 0, i32 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8, !align !3
// CHECK:   %[[VAL_3:.*]] = getelementptr {{[{]}} ptr, ptr, i64, i64 }, ptr %[[VAL_1]], i32 0, i32 1
// CHECK:   %[[VAL_4:.*]] = load ptr, ptr %[[VAL_3]], align 8, !align !3
// CHECK:   %[[VAL_5:.*]] = getelementptr {{[{]}} ptr, ptr, i64, i64 }, ptr %[[VAL_1]], i32 0, i32 2
// CHECK:   %[[VAL_6:.*]] = load i64, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_7:.*]] = getelementptr {{[{]}} ptr, ptr, i64, i64 }, ptr %[[VAL_1]], i32 0, i32 3
// CHECK:   %[[VAL_8:.*]] = load i64, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_9:.*]] = alloca %[[VAL_10:.*]], align 8
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: target.task.body:
// CHECK:   %[[VAL_12:.*]] = getelementptr inbounds nuw %[[VAL_10]], ptr %[[VAL_9]], i32 0, i32 0
// CHECK:   store i32 3, ptr %[[VAL_12]], align 4
// CHECK:   %[[VAL_13:.*]] = getelementptr inbounds nuw %[[VAL_10]], ptr %[[VAL_9]], i32 0, i32 1
// CHECK:   store i32 4, ptr %[[VAL_13]], align 4
// CHECK:   %[[VAL_14:.*]] = getelementptr inbounds nuw %[[VAL_10]], ptr %[[VAL_9]], i32 0, i32 2
// CHECK:   store ptr %[[VAL_2]], ptr %[[VAL_14]], align 8
// CHECK:   %[[VAL_15:.*]] = getelementptr inbounds nuw %[[VAL_10]], ptr %[[VAL_9]], i32 0, i32 3
// CHECK:   store ptr %[[VAL_4]], ptr %[[VAL_15]], align 8
// CHECK:   %[[VAL_16:.*]] = getelementptr inbounds nuw %[[VAL_10]], ptr %[[VAL_9]], i32 0, i32 4
// CHECK:   store ptr @.offload_sizes, ptr %[[VAL_16]], align 8
// CHECK:   %[[VAL_17:.*]] = getelementptr inbounds nuw %[[VAL_10]], ptr %[[VAL_9]], i32 0, i32 5
// CHECK:   store ptr @.offload_maptypes, ptr %[[VAL_17]], align 8
// CHECK:   %[[VAL_18:.*]] = getelementptr inbounds nuw %[[VAL_10]], ptr %[[VAL_9]], i32 0, i32 6
// CHECK:   store ptr @.offload_mapnames, ptr %[[VAL_18]], align 8
// CHECK:   %[[VAL_19:.*]] = getelementptr inbounds nuw %[[VAL_10]], ptr %[[VAL_9]], i32 0, i32 7
// CHECK:   store ptr null, ptr %[[VAL_19]], align 8
// CHECK:   %[[VAL_20:.*]] = getelementptr inbounds nuw %[[VAL_10]], ptr %[[VAL_9]], i32 0, i32 8
// CHECK:   store i64 0, ptr %[[VAL_20]], align 8
// CHECK:   %[[VAL_21:.*]] = getelementptr inbounds nuw %[[VAL_10]], ptr %[[VAL_9]], i32 0, i32 9
// CHECK:   store i64 0, ptr %[[VAL_21]], align 8
// CHECK:   %[[VAL_22:.*]] = getelementptr inbounds nuw %[[VAL_10]], ptr %[[VAL_9]], i32 0, i32 10
// CHECK:   store [3 x i32] [i32 -1, i32 0, i32 0], ptr %[[VAL_22]], align 4
// CHECK:   %[[VAL_23:.*]] = getelementptr inbounds nuw %[[VAL_10]], ptr %[[VAL_9]], i32 0, i32 11
// CHECK:   store [3 x i32] zeroinitializer, ptr %[[VAL_23]], align 4
// CHECK:   %[[VAL_24:.*]] = getelementptr inbounds nuw %[[VAL_10]], ptr %[[VAL_9]], i32 0, i32 12
// CHECK:   store i32 0, ptr %[[VAL_24]], align 4
// CHECK:   %[[VAL_25:.*]] = call i32 @__tgt_target_kernel(ptr @1, i64 -1, i32 -1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}__QQmain_l{{.*}}.region_id, ptr %[[VAL_9]])
// CHECK:   %[[VAL_26:.*]] = icmp ne i32 %[[VAL_25]], 0
// CHECK:   br i1 %[[VAL_26]], label %[[VAL_27:.*]], label %[[VAL_28:.*]]
// CHECK: omp_offload.cont:
// CHECK:   br label %[[VAL_29:.*]]
// CHECK: omp_offload.failed:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QQmain_l{{.*}}(ptr @_QFEa, ptr @_QFEb, i64 %[[VAL_6]], i64 %[[VAL_8]])
// CHECK:   br label %[[VAL_28]]
// CHECK: target.task.cont.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @_QQmain..omp_par(
// CHECK: task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region:
// CHECK:   %[[VAL_2:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr @_QFEn, align 4
// CHECK:   %[[VAL_4:.*]] = sext i32 %[[VAL_3]] to i64
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.task.region1:
// CHECK:   %[[VAL_6:.*]] = phi i32 [ %[[VAL_7:.*]], %[[VAL_8:.*]] ], [ 1, %[[VAL_1]] ]
// CHECK:   %[[VAL_9:.*]] = phi i64 [ %[[VAL_10:.*]], %[[VAL_8]] ], [ %[[VAL_4]], %[[VAL_1]] ]
// CHECK:   %[[VAL_11:.*]] = icmp sgt i64 %[[VAL_9]], 0
// CHECK:   br i1 %[[VAL_11]], label %[[VAL_8]], label %[[VAL_12:.*]]
// CHECK: omp.task.region3:
// CHECK:   store i32 %[[VAL_6]], ptr %[[VAL_2]], align 4
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.task.region2:
// CHECK:   store i32 %[[VAL_6]], ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_15:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_16:.*]] = sext i32 %[[VAL_15]] to i64
// CHECK:   %[[VAL_17:.*]] = sub nsw i64 %[[VAL_16]], 1
// CHECK:   %[[VAL_18:.*]] = mul nsw i64 %[[VAL_17]], 1
// CHECK:   %[[VAL_19:.*]] = mul nsw i64 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = add nsw i64 %[[VAL_19]], 0
// CHECK:   %[[VAL_21:.*]] = getelementptr i32, ptr @_QFEa, i64 %[[VAL_20]]
// CHECK:   store i32 %[[VAL_15]], ptr %[[VAL_21]], align 4
// CHECK:   %[[VAL_22:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_7]] = add i32 %[[VAL_22]], 1
// CHECK:   %[[VAL_10]] = sub i64 %[[VAL_9]], 1
// CHECK:   br label %[[VAL_5]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define i32 @main(
// CHECK:   call void @_FortranAProgramStart(i32 %[[VAL_0:.*]], ptr %[[VAL_1:.*]], ptr %[[VAL_2:.*]], ptr null)
// CHECK:   call void @_QQmain()
// CHECK:   call void @_FortranAProgramEndStatement()
// CHECK:   ret i32 0
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QQmain_l{{.*}}(
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
// CHECK: omp.target3:
// CHECK:   store i32 %[[VAL_7:.*]], ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.target2:
// CHECK:   store i32 %[[VAL_7]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_9:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_10:.*]] = sext i32 %[[VAL_9]] to i64
// CHECK:   %[[VAL_11:.*]] = sub nsw i64 %[[VAL_10]], 1
// CHECK:   %[[VAL_12:.*]] = mul nsw i64 %[[VAL_11]], 1
// CHECK:   %[[VAL_13:.*]] = mul nsw i64 %[[VAL_12]], 1
// CHECK:   %[[VAL_14:.*]] = add nsw i64 %[[VAL_13]], 0
// CHECK:   %[[VAL_15:.*]] = getelementptr i32, ptr %[[VAL_16:.*]], i64 %[[VAL_14]]
// CHECK:   %[[VAL_17:.*]] = load i32, ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_18:.*]] = add i32 %[[VAL_17]], 10
// CHECK:   %[[VAL_19:.*]] = sub nsw i64 %[[VAL_10]], 1
// CHECK:   %[[VAL_20:.*]] = mul nsw i64 %[[VAL_19]], 1
// CHECK:   %[[VAL_21:.*]] = mul nsw i64 %[[VAL_20]], 1
// CHECK:   %[[VAL_22:.*]] = add nsw i64 %[[VAL_21]], 0
// CHECK:   %[[VAL_23:.*]] = getelementptr i32, ptr %[[VAL_24:.*]], i64 %[[VAL_22]]
// CHECK:   store i32 %[[VAL_18]], ptr %[[VAL_23]], align 4
// CHECK:   %[[VAL_25:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_26:.*]] = add i32 %[[VAL_25]], 1
// CHECK:   %[[VAL_27:.*]] = sub i64 %[[VAL_28:.*]], 1
// CHECK:   br label %[[VAL_29:.*]]
// CHECK: omp.target1:
// CHECK:   %[[VAL_7]] = phi i32 [ %[[VAL_26]], %[[VAL_30:.*]] ], [ 1, %[[VAL_6]] ]
// CHECK:   %[[VAL_28]] = phi i64 [ %[[VAL_27]], %[[VAL_30]] ], [ %[[VAL_31:.*]], %[[VAL_6]] ]
// CHECK:   %[[VAL_32:.*]] = icmp sgt i64 %[[VAL_28]], 0
// CHECK:   br i1 %[[VAL_32]], label %[[VAL_30]], label %[[VAL_33:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_34:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_31]] = sext i32 %[[VAL_34]] to i64
// CHECK:   br label %[[VAL_29]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @.omp_target_task_proxy_func(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr, ptr, i64, i64 }, align 8
// CHECK:   %[[VAL_1:.*]] = getelementptr inbounds nuw %[[VAL_2:.*]], ptr %[[VAL_3:.*]], i32 0, i32 0
// CHECK:   %[[VAL_4:.*]] = load ptr, ptr %[[VAL_1]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 8 %[[VAL_0]], ptr align 1 %[[VAL_4]], i64 32, i1 false)
// CHECK:   call void @_QQmain..omp_par.1(i32 %[[VAL_5:.*]], ptr %[[VAL_0]])
// CHECK:   ret void
// CHECK: }
