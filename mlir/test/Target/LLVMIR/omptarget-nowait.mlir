// RUN: mlir-translate -mlir-to-llvmir %s 2>&1 | FileCheck %s

module attributes {omp.target_triples = ["amdgcn-amd-amdhsa"]} {
  llvm.func @launch_(%arg0: !llvm.ptr {fir.bindc_name = "a", llvm.nocapture}) {
    %0 = llvm.mlir.constant(1 : i64) : i64
    %1 = llvm.alloca %0 x f64 {bindc_name = "n"} : (i64) -> !llvm.ptr
    %2 = llvm.getelementptr %arg0[0, 0] : (!llvm.ptr) -> !llvm.ptr, !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8, array<1 x array<3 x i64>>)>
    %3 = omp.map.info var_ptr(%arg0 : !llvm.ptr, f64) map_clauses(tofrom) capture(ByRef) var_ptr_ptr(%2 : !llvm.ptr)  -> !llvm.ptr {name = ""}
    %4 = omp.map.info var_ptr(%arg0 : !llvm.ptr, !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8, array<1 x array<3 x i64>>)>) map_clauses(to) capture(ByRef) members(%3 : [0] : !llvm.ptr) -> !llvm.ptr {name = "a"}
    %5 = omp.map.info var_ptr(%1 : !llvm.ptr, f64) map_clauses(implicit, exit_release_or_enter_alloc) capture(ByCopy) -> !llvm.ptr {name = "n"}
    omp.target nowait map_entries(%4 -> %arg1, %5 -> %arg2, %3 -> %arg3 : !llvm.ptr, !llvm.ptr, !llvm.ptr) {
      %two_f = llvm.mlir.constant(2.000000e+00 : f64) : f64
      %one_i = llvm.mlir.constant(1 : index) : i64
      %6 = llvm.getelementptr %arg1[0, 0] : (!llvm.ptr) -> !llvm.ptr, !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8, array<1 x array<3 x i64>>)>
      %7 = llvm.load %6 : !llvm.ptr -> !llvm.ptr
      %8 = llvm.getelementptr %7[%one_i] : (!llvm.ptr, i64) -> !llvm.ptr, i8
      %9 = llvm.load %8 : !llvm.ptr -> f64
      %10 = llvm.fmul %9, %two_f {fastmathFlags = #llvm.fastmath<contract>} : f64
      llvm.store %10, %8 : f64, !llvm.ptr
      omp.terminator
    }
    llvm.return
  }
}

// CHECK: @0 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @1 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @2 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @.__omp_offloading_{{.*}}_{{.*}}_launch__l{{.*}}.region_id = weak constant i8 0
// CHECK: @.offload_sizes = private unnamed_addr constant [6 x i64] [i64 0, i64 0, i64 0, i64 8, i64 0, i64 8]
// CHECK: @.offload_maptypes = private unnamed_addr constant [6 x i64] [i64 32, i64 281474976710657, i64 281474976710657, i64 281474976710659, i64 281474976710675, i64 800]
// CHECK: @.offload_mapnames = private constant [6 x ptr] [ptr @0, ptr @0, ptr @0, ptr @2, ptr @2, ptr @1]
// CHECK: @3 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @4 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @3 }, align 8
// CHECK: @.offloading.entry_name = internal unnamed_addr constant [{{.*}} x i8] c"__omp_offloading_{{.*}}_{{.*}}_launch__l{{.*}}\00", section ".llvm.rodata.offloading", align 1
// CHECK: @.offloading.entry.__omp_offloading_{{.*}}_{{.*}}_launch__l{{.*}} = weak constant %struct.__tgt_offload_entry {{[{]}} i64 0, i16 1, i16 1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}_launch__l{{.*}}.region_id, ptr @.offloading.entry_name, i64 0, i64 0, ptr null }, section "llvm_offload_entries", align 8
// CHECK-LABEL: define void @launch_(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr, i64 }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca [6 x ptr], align 8
// CHECK:   %[[VAL_3:.*]] = alloca [6 x ptr], align 8
// CHECK:   %[[VAL_4:.*]] = alloca [6 x ptr], align 8
// CHECK:   %[[VAL_5:.*]] = alloca [6 x i64], align 4
// CHECK:   %[[VAL_6:.*]] = alloca double, i64 1, align 8
// CHECK:   %[[VAL_7:.*]] = getelementptr {{[{]}} ptr, i64, i32, i8, i8, i8, i8, [1 x [3 x i64]] }, ptr %[[VAL_8:.*]], i32 0, i32 0
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_10:.*]] = load double, ptr %[[VAL_6]], align 8
// CHECK:   store double %[[VAL_10]], ptr %[[VAL_1]], align 8
// CHECK:   %[[VAL_11:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_12:.*]] = load ptr, ptr %[[VAL_7]], align 8
// CHECK:   %[[VAL_13:.*]] = getelementptr {{[{]}} ptr, i64, i32, i8, i8, i8, i8, [1 x [3 x i64]] }, ptr %[[VAL_8]], i32 1
// CHECK:   %[[VAL_14:.*]] = ptrtoint ptr %[[VAL_13]] to i64
// CHECK:   %[[VAL_15:.*]] = ptrtoint ptr %[[VAL_8]] to i64
// CHECK:   %[[VAL_16:.*]] = sub i64 %[[VAL_14]], %[[VAL_15]]
// CHECK:   %[[VAL_17:.*]] = sdiv exact i64 %[[VAL_16]], ptrtoint (ptr getelementptr (i8, ptr null, i32 1) to i64)
// CHECK:   %[[VAL_18:.*]] = getelementptr {{[{]}} ptr, i64, i32, i8, i8, i8, i8, [1 x [3 x i64]] }, ptr %[[VAL_8]], i32 1
// CHECK:   %[[VAL_19:.*]] = ptrtoint ptr %[[VAL_7]] to i64
// CHECK:   %[[VAL_20:.*]] = ptrtoint ptr %[[VAL_8]] to i64
// CHECK:   %[[VAL_21:.*]] = sub i64 %[[VAL_19]], %[[VAL_20]]
// CHECK:   %[[VAL_22:.*]] = sdiv exact i64 %[[VAL_21]], ptrtoint (ptr getelementptr (i8, ptr null, i32 1) to i64)
// CHECK:   %[[VAL_23:.*]] = getelementptr ptr, ptr %[[VAL_7]], i32 1
// CHECK:   %[[VAL_24:.*]] = ptrtoint ptr %[[VAL_18]] to i64
// CHECK:   %[[VAL_25:.*]] = ptrtoint ptr %[[VAL_23]] to i64
// CHECK:   %[[VAL_26:.*]] = sub i64 %[[VAL_24]], %[[VAL_25]]
// CHECK:   %[[VAL_27:.*]] = sdiv exact i64 %[[VAL_26]], ptrtoint (ptr getelementptr (i8, ptr null, i32 1) to i64)
// CHECK:   %[[VAL_28:.*]] = icmp eq ptr %[[VAL_12]], null
// CHECK:   %[[VAL_29:.*]] = select i1 %[[VAL_28]], i64 0, i64 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 8 %[[VAL_5]], ptr align 4 @.offload_sizes, i64 48, i1 false)
// CHECK:   %[[VAL_30:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_2]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_8]], ptr %[[VAL_30]], align 8
// CHECK:   %[[VAL_31:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_3]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_8]], ptr %[[VAL_31]], align 8
// CHECK:   %[[VAL_32:.*]] = getelementptr inbounds [6 x i64], ptr %[[VAL_5]], i32 0, i32 0
// CHECK:   store i64 %[[VAL_17]], ptr %[[VAL_32]], align 8
// CHECK:   %[[VAL_33:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_4]], i64 0, i64 0
// CHECK:   store ptr null, ptr %[[VAL_33]], align 8
// CHECK:   %[[VAL_34:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_2]], i32 0, i32 1
// CHECK:   store ptr %[[VAL_8]], ptr %[[VAL_34]], align 8
// CHECK:   %[[VAL_35:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_3]], i32 0, i32 1
// CHECK:   store ptr %[[VAL_8]], ptr %[[VAL_35]], align 8
// CHECK:   %[[VAL_36:.*]] = getelementptr inbounds [6 x i64], ptr %[[VAL_5]], i32 0, i32 1
// CHECK:   store i64 %[[VAL_22]], ptr %[[VAL_36]], align 8
// CHECK:   %[[VAL_37:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_4]], i64 0, i64 1
// CHECK:   store ptr null, ptr %[[VAL_37]], align 8
// CHECK:   %[[VAL_38:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_2]], i32 0, i32 2
// CHECK:   store ptr %[[VAL_8]], ptr %[[VAL_38]], align 8
// CHECK:   %[[VAL_39:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_3]], i32 0, i32 2
// CHECK:   store ptr %[[VAL_23]], ptr %[[VAL_39]], align 8
// CHECK:   %[[VAL_40:.*]] = getelementptr inbounds [6 x i64], ptr %[[VAL_5]], i32 0, i32 2
// CHECK:   store i64 %[[VAL_27]], ptr %[[VAL_40]], align 8
// CHECK:   %[[VAL_41:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_4]], i64 0, i64 2
// CHECK:   store ptr null, ptr %[[VAL_41]], align 8
// CHECK:   %[[VAL_42:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_2]], i32 0, i32 3
// CHECK:   store ptr %[[VAL_8]], ptr %[[VAL_42]], align 8
// CHECK:   %[[VAL_43:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_3]], i32 0, i32 3
// CHECK:   store ptr %[[VAL_7]], ptr %[[VAL_43]], align 8
// CHECK:   %[[VAL_44:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_4]], i64 0, i64 3
// CHECK:   store ptr null, ptr %[[VAL_44]], align 8
// CHECK:   %[[VAL_45:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_2]], i32 0, i32 4
// CHECK:   store ptr %[[VAL_7]], ptr %[[VAL_45]], align 8
// CHECK:   %[[VAL_46:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_3]], i32 0, i32 4
// CHECK:   store ptr %[[VAL_12]], ptr %[[VAL_46]], align 8
// CHECK:   %[[VAL_47:.*]] = getelementptr inbounds [6 x i64], ptr %[[VAL_5]], i32 0, i32 4
// CHECK:   store i64 %[[VAL_29]], ptr %[[VAL_47]], align 8
// CHECK:   %[[VAL_48:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_4]], i64 0, i64 4
// CHECK:   store ptr null, ptr %[[VAL_48]], align 8
// CHECK:   %[[VAL_49:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_2]], i32 0, i32 5
// CHECK:   store i64 %[[VAL_11]], ptr %[[VAL_49]], align 8
// CHECK:   %[[VAL_50:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_3]], i32 0, i32 5
// CHECK:   store i64 %[[VAL_11]], ptr %[[VAL_50]], align 8
// CHECK:   %[[VAL_51:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_4]], i64 0, i64 5
// CHECK:   store ptr null, ptr %[[VAL_51]], align 8
// CHECK:   %[[VAL_52:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_2]], i32 0, i32 0
// CHECK:   %[[VAL_53:.*]] = getelementptr inbounds [6 x ptr], ptr %[[VAL_3]], i32 0, i32 0
// CHECK:   %[[VAL_54:.*]] = getelementptr inbounds [6 x i64], ptr %[[VAL_5]], i32 0, i32 0
// CHECK:   br label %[[VAL_55:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_56:.*]] = getelementptr {{[{]}} ptr, i64 }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_8]], ptr %[[VAL_56]], align 8
// CHECK:   %[[VAL_57:.*]] = getelementptr {{[{]}} ptr, i64 }, ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   store i64 %[[VAL_11]], ptr %[[VAL_57]], align 4
// CHECK:   %[[VAL_58:.*]] = call i32 @__kmpc_global_thread_num(ptr @4)
// CHECK:   %[[VAL_59:.*]] = call ptr @__kmpc_omp_target_task_alloc(ptr @4, i32 %[[VAL_58]], i32 0, i64 184, i64 16, ptr @.omp_target_task_proxy_func, i64 -1)
// CHECK:   %[[VAL_60:.*]] = getelementptr inbounds nuw %[[VAL_61:.*]], ptr %[[VAL_59]], i32 0, i32 0
// CHECK:   %[[VAL_62:.*]] = getelementptr inbounds nuw %[[VAL_63:.*]], ptr %[[VAL_60]], i32 0, i32 0
// CHECK:   %[[VAL_64:.*]] = load ptr, ptr %[[VAL_62]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_64]], ptr align 1 %[[VAL_0]], i64 16, i1 false)
// CHECK:   %[[VAL_65:.*]] = getelementptr inbounds nuw %[[VAL_61]], ptr %[[VAL_59]], i32 0, i32 1
// CHECK:   %[[VAL_66:.*]] = getelementptr inbounds nuw %[[VAL_67:.*]], ptr %[[VAL_65]], i32 0, i32 0
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_66]], ptr align 1 %[[VAL_52]], i64 48, i1 false)
// CHECK:   %[[VAL_68:.*]] = getelementptr inbounds nuw %[[VAL_67]], ptr %[[VAL_65]], i32 0, i32 1
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_68]], ptr align 1 %[[VAL_53]], i64 48, i1 false)
// CHECK:   %[[VAL_69:.*]] = getelementptr inbounds nuw %[[VAL_67]], ptr %[[VAL_65]], i32 0, i32 2
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_69]], ptr align 1 %[[VAL_54]], i64 48, i1 false)
// CHECK:   %[[VAL_70:.*]] = call i32 @__kmpc_omp_task(ptr @4, i32 %[[VAL_58]], ptr %[[VAL_59]])
// CHECK:   br label %[[VAL_71:.*]]
// CHECK: target.task.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @launch_..omp_par(
// CHECK: target.task.alloca:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr, i64 }, ptr %[[VAL_1:.*]], i32 0, i32 0
// CHECK:   %[[VAL_2:.*]] = load ptr, ptr %[[VAL_0]], align 8, !align !3
// CHECK:   %[[VAL_3:.*]] = getelementptr {{[{]}} ptr, i64 }, ptr %[[VAL_1]], i32 0, i32 1
// CHECK:   %[[VAL_4:.*]] = load i64, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_5:.*]] = alloca %[[VAL_6:.*]], align 8
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: target.task.body:
// CHECK:   %[[VAL_8:.*]] = getelementptr inbounds nuw %[[VAL_6]], ptr %[[VAL_5]], i32 0, i32 0
// CHECK:   store i32 3, ptr %[[VAL_8]], align 4
// CHECK:   %[[VAL_9:.*]] = getelementptr inbounds nuw %[[VAL_6]], ptr %[[VAL_5]], i32 0, i32 1
// CHECK:   store i32 6, ptr %[[VAL_9]], align 4
// CHECK:   %[[VAL_10:.*]] = getelementptr inbounds nuw %[[VAL_6]], ptr %[[VAL_5]], i32 0, i32 2
// CHECK:   store ptr %[[VAL_11:.*]], ptr %[[VAL_10]], align 8
// CHECK:   %[[VAL_12:.*]] = getelementptr inbounds nuw %[[VAL_6]], ptr %[[VAL_5]], i32 0, i32 3
// CHECK:   store ptr %[[VAL_13:.*]], ptr %[[VAL_12]], align 8
// CHECK:   %[[VAL_14:.*]] = getelementptr inbounds nuw %[[VAL_6]], ptr %[[VAL_5]], i32 0, i32 4
// CHECK:   store ptr %[[VAL_15:.*]], ptr %[[VAL_14]], align 8
// CHECK:   %[[VAL_16:.*]] = getelementptr inbounds nuw %[[VAL_6]], ptr %[[VAL_5]], i32 0, i32 5
// CHECK:   store ptr @.offload_maptypes, ptr %[[VAL_16]], align 8
// CHECK:   %[[VAL_17:.*]] = getelementptr inbounds nuw %[[VAL_6]], ptr %[[VAL_5]], i32 0, i32 6
// CHECK:   store ptr @.offload_mapnames, ptr %[[VAL_17]], align 8
// CHECK:   %[[VAL_18:.*]] = getelementptr inbounds nuw %[[VAL_6]], ptr %[[VAL_5]], i32 0, i32 7
// CHECK:   store ptr null, ptr %[[VAL_18]], align 8
// CHECK:   %[[VAL_19:.*]] = getelementptr inbounds nuw %[[VAL_6]], ptr %[[VAL_5]], i32 0, i32 8
// CHECK:   store i64 0, ptr %[[VAL_19]], align 8
// CHECK:   %[[VAL_20:.*]] = getelementptr inbounds nuw %[[VAL_6]], ptr %[[VAL_5]], i32 0, i32 9
// CHECK:   store i64 1, ptr %[[VAL_20]], align 8
// CHECK:   %[[VAL_21:.*]] = getelementptr inbounds nuw %[[VAL_6]], ptr %[[VAL_5]], i32 0, i32 10
// CHECK:   store [3 x i32] [i32 -1, i32 0, i32 0], ptr %[[VAL_21]], align 4
// CHECK:   %[[VAL_22:.*]] = getelementptr inbounds nuw %[[VAL_6]], ptr %[[VAL_5]], i32 0, i32 11
// CHECK:   store [3 x i32] zeroinitializer, ptr %[[VAL_22]], align 4
// CHECK:   %[[VAL_23:.*]] = getelementptr inbounds nuw %[[VAL_6]], ptr %[[VAL_5]], i32 0, i32 12
// CHECK:   store i32 0, ptr %[[VAL_23]], align 4
// CHECK:   %[[VAL_24:.*]] = call i32 @__tgt_target_kernel(ptr @4, i64 -1, i32 -1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}_launch__l{{.*}}.region_id, ptr %[[VAL_5]])
// CHECK:   %[[VAL_25:.*]] = icmp ne i32 %[[VAL_24]], 0
// CHECK:   br i1 %[[VAL_25]], label %[[VAL_26:.*]], label %[[VAL_27:.*]]
// CHECK: omp_offload.cont:
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_offload.failed:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_launch__l{{.*}}(ptr %[[VAL_2]], i64 %[[VAL_4]])
// CHECK:   br label %[[VAL_27]]
// CHECK: target.task.cont.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_launch__l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_5:.*]] = getelementptr {{[{]}} ptr, i64, i32, i8, i8, i8, i8, [1 x [3 x i64]] }, ptr %[[VAL_6:.*]], i32 0, i32 0
// CHECK:   %[[VAL_7:.*]] = load ptr, ptr %[[VAL_5]], align 8
// CHECK:   %[[VAL_8:.*]] = getelementptr i8, ptr %[[VAL_7]], i64 1
// CHECK:   %[[VAL_9:.*]] = load double, ptr %[[VAL_8]], align 8
// CHECK:   %[[VAL_10:.*]] = fmul contract double %[[VAL_9]], 2.000000e+00
// CHECK:   store double %[[VAL_10]], ptr %[[VAL_8]], align 8
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @.omp_target_task_proxy_func(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = getelementptr inbounds nuw %[[VAL_1:.*]], ptr %[[VAL_2:.*]], i32 0, i32 1
// CHECK:   %[[VAL_3:.*]] = getelementptr inbounds nuw %[[VAL_4:.*]], ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   %[[VAL_5:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   %[[VAL_6:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   %[[VAL_7:.*]] = alloca {{[{]}} ptr, i64 }, align 8
// CHECK:   %[[VAL_8:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_2]], i32 0, i32 0
// CHECK:   %[[VAL_9:.*]] = getelementptr inbounds nuw %[[VAL_10:.*]], ptr %[[VAL_8]], i32 0, i32 0
// CHECK:   %[[VAL_11:.*]] = load ptr, ptr %[[VAL_9]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 8 %[[VAL_7]], ptr align 1 %[[VAL_11]], i64 16, i1 false)
// CHECK:   call void @launch_..omp_par(i32 %[[VAL_12:.*]], ptr %[[VAL_3]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]])
// CHECK:   ret void
// CHECK: }
