// RUN: mlir-translate -mlir-to-llvmir %s 2>&1 | FileCheck %s

// Set a dummy target triple to enable target region outlining.
module attributes {omp.target_triples = ["dummy-target-triple"]} {
  llvm.func @_QPfoo() {
    %0 = llvm.mlir.constant(1 : i64) : i64
    %1 = llvm.alloca %0 x i32 : (i64) -> !llvm.ptr
    %2 = omp.map.info var_ptr(%1 : !llvm.ptr, i32) map_clauses(implicit) capture(ByCopy) -> !llvm.ptr
    omp.target nowait map_entries(%2 -> %arg0 : !llvm.ptr) {
      %3 = llvm.mlir.constant(2 : i32) : i32
      llvm.store %3, %arg0 : i32, !llvm.ptr
      omp.terminator
    }
    llvm.return
  }
}

// CHECK: @0 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @.__omp_offloading_{{.*}}_{{.*}}__QPfoo_l{{.*}}.region_id = weak constant i8 0
// CHECK: @.offload_sizes = private unnamed_addr constant [1 x i64] [i64 4]
// CHECK: @.offload_maptypes = private unnamed_addr constant [1 x i64] [i64 800]
// CHECK: @.offload_mapnames = private constant [1 x ptr] [ptr @0]
// CHECK: @1 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @1 }, align 8
// CHECK: @.offloading.entry_name = internal unnamed_addr constant [{{.*}} x i8] c"__omp_offloading_{{.*}}_{{.*}}__QPfoo_l{{.*}}\00", section ".llvm.rodata.offloading", align 1
// CHECK: @.offloading.entry.__omp_offloading_{{.*}}_{{.*}}__QPfoo_l{{.*}} = weak constant %struct.__tgt_offload_entry {{[{]}} i64 0, i16 1, i16 1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}__QPfoo_l{{.*}}.region_id, ptr @.offloading.entry_name, i64 0, i64 0, ptr null }, section "llvm_offload_entries", align 8
// CHECK-LABEL: define void @_QPfoo(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} i64 }, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_2:.*]] = alloca [1 x ptr], align 8
// CHECK:   %[[VAL_3:.*]] = alloca [1 x ptr], align 8
// CHECK:   %[[VAL_4:.*]] = alloca [1 x ptr], align 8
// CHECK:   %[[VAL_5:.*]] = alloca i32, i64 1, align 4
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_7:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   store i32 %[[VAL_7]], ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_8:.*]] = load i64, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_9:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_2]], i32 0, i32 0
// CHECK:   store i64 %[[VAL_8]], ptr %[[VAL_9]], align 8
// CHECK:   %[[VAL_10:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_3]], i32 0, i32 0
// CHECK:   store i64 %[[VAL_8]], ptr %[[VAL_10]], align 8
// CHECK:   %[[VAL_11:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_4]], i64 0, i64 0
// CHECK:   store ptr null, ptr %[[VAL_11]], align 8
// CHECK:   %[[VAL_12:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_2]], i32 0, i32 0
// CHECK:   %[[VAL_13:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_3]], i32 0, i32 0
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_15:.*]] = getelementptr {{[{]}} i64 }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store i64 %[[VAL_8]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_16:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// CHECK:   %[[VAL_17:.*]] = call ptr @__kmpc_omp_target_task_alloc(ptr @2, i32 %[[VAL_16]], i32 0, i64 56, i64 8, ptr @.omp_target_task_proxy_func, i64 -1)
// CHECK:   %[[VAL_18:.*]] = getelementptr inbounds nuw %[[VAL_19:.*]], ptr %[[VAL_17]], i32 0, i32 0
// CHECK:   %[[VAL_20:.*]] = getelementptr inbounds nuw %[[VAL_21:.*]], ptr %[[VAL_18]], i32 0, i32 0
// CHECK:   %[[VAL_22:.*]] = load ptr, ptr %[[VAL_20]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_22]], ptr align 1 %[[VAL_0]], i64 8, i1 false)
// CHECK:   %[[VAL_23:.*]] = getelementptr inbounds nuw %[[VAL_19]], ptr %[[VAL_17]], i32 0, i32 1
// CHECK:   %[[VAL_24:.*]] = getelementptr inbounds nuw %[[VAL_25:.*]], ptr %[[VAL_23]], i32 0, i32 0
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_24]], ptr align 1 %[[VAL_12]], i64 8, i1 false)
// CHECK:   %[[VAL_26:.*]] = getelementptr inbounds nuw %[[VAL_25]], ptr %[[VAL_23]], i32 0, i32 1
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_26]], ptr align 1 %[[VAL_13]], i64 8, i1 false)
// CHECK:   %[[VAL_27:.*]] = call i32 @__kmpc_omp_task(ptr @2, i32 %[[VAL_16]], ptr %[[VAL_17]])
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: target.task.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @_QPfoo..omp_par(
// CHECK: target.task.alloca:
// CHECK:   %[[VAL_0:.*]] = getelementptr {{[{]}} i64 }, ptr %[[VAL_1:.*]], i32 0, i32 0
// CHECK:   %[[VAL_2:.*]] = load i64, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = alloca %[[VAL_4:.*]], align 8
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: target.task.body:
// CHECK:   %[[VAL_6:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 0
// CHECK:   store i32 3, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_7:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 1
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_8:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 2
// CHECK:   store ptr %[[VAL_9:.*]], ptr %[[VAL_8]], align 8
// CHECK:   %[[VAL_10:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 3
// CHECK:   store ptr %[[VAL_11:.*]], ptr %[[VAL_10]], align 8
// CHECK:   %[[VAL_12:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 4
// CHECK:   store ptr @.offload_sizes, ptr %[[VAL_12]], align 8
// CHECK:   %[[VAL_13:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 5
// CHECK:   store ptr @.offload_maptypes, ptr %[[VAL_13]], align 8
// CHECK:   %[[VAL_14:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 6
// CHECK:   store ptr @.offload_mapnames, ptr %[[VAL_14]], align 8
// CHECK:   %[[VAL_15:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 7
// CHECK:   store ptr null, ptr %[[VAL_15]], align 8
// CHECK:   %[[VAL_16:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 8
// CHECK:   store i64 0, ptr %[[VAL_16]], align 8
// CHECK:   %[[VAL_17:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 9
// CHECK:   store i64 1, ptr %[[VAL_17]], align 8
// CHECK:   %[[VAL_18:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 10
// CHECK:   store [3 x i32] [i32 -1, i32 0, i32 0], ptr %[[VAL_18]], align 4
// CHECK:   %[[VAL_19:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 11
// CHECK:   store [3 x i32] zeroinitializer, ptr %[[VAL_19]], align 4
// CHECK:   %[[VAL_20:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 12
// CHECK:   store i32 0, ptr %[[VAL_20]], align 4
// CHECK:   %[[VAL_21:.*]] = call i32 @__tgt_target_kernel(ptr @2, i64 -1, i32 -1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}__QPfoo_l{{.*}}.region_id, ptr %[[VAL_3]])
// CHECK:   %[[VAL_22:.*]] = icmp ne i32 %[[VAL_21]], 0
// CHECK:   br i1 %[[VAL_22]], label %[[VAL_23:.*]], label %[[VAL_24:.*]]
// CHECK: omp_offload.cont:
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp_offload.failed:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QPfoo_l{{.*}}(i64 %[[VAL_2]])
// CHECK:   br label %[[VAL_24]]
// CHECK: target.task.cont.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QPfoo_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.target:
// CHECK:   store i32 2, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @.omp_target_task_proxy_func(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = getelementptr inbounds nuw %[[VAL_1:.*]], ptr %[[VAL_2:.*]], i32 0, i32 1
// CHECK:   %[[VAL_3:.*]] = getelementptr inbounds nuw %[[VAL_4:.*]], ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   %[[VAL_5:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   %[[VAL_6:.*]] = alloca {{[{]}} i64 }, align 8
// CHECK:   %[[VAL_7:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_2]], i32 0, i32 0
// CHECK:   %[[VAL_8:.*]] = getelementptr inbounds nuw %[[VAL_9:.*]], ptr %[[VAL_7]], i32 0, i32 0
// CHECK:   %[[VAL_10:.*]] = load ptr, ptr %[[VAL_8]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 8 %[[VAL_6]], ptr align 1 %[[VAL_10]], i64 8, i1 false)
// CHECK:   call void @_QPfoo..omp_par(i32 %[[VAL_11:.*]], ptr %[[VAL_3]], ptr %[[VAL_5]], ptr %[[VAL_6]])
// CHECK:   ret void
// CHECK: }
