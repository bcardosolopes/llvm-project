// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

module attributes {omp.is_target_device = false, omp.target_triples = ["amdgcn-amd-amdhsa"]} {
  llvm.func @_QQmain() attributes {fir.bindc_name = "main"} {
    %0 = llvm.mlir.addressof @_QFEi : !llvm.ptr
    %1 = llvm.mlir.addressof @_QFEsp : !llvm.ptr
    %2 = omp.map.info var_ptr(%1 : !llvm.ptr, i32) map_clauses(tofrom) capture(ByRef) -> !llvm.ptr {name = "sp"}
    %3 = omp.map.info var_ptr(%0 : !llvm.ptr, i32) map_clauses(to) capture(ByCopy) -> !llvm.ptr {name = "i"}
    omp.target map_entries(%2 -> %arg0, %3 -> %arg1 : !llvm.ptr, !llvm.ptr) {
      %4 = llvm.load %arg1 : !llvm.ptr -> i32
      llvm.store %4, %arg0 : i32, !llvm.ptr
      omp.terminator
    }
    llvm.return
  }
  llvm.mlir.global internal @_QFEi() {addr_space = 0 : i32} : i32 {
    %0 = llvm.mlir.constant(1 : i32) : i32
    llvm.return %0 : i32
  }
  llvm.mlir.global internal @_QFEsp() {addr_space = 0 : i32} : i32 {
    %0 = llvm.mlir.constant(0 : i32) : i32
    llvm.return %0 : i32
  }
}

// CHECK: @_QFEi = internal global i32 1
// CHECK: @_QFEsp = internal global i32 0
// CHECK: @0 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @1 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @.__omp_offloading_{{.*}}_{{.*}}__QQmain_l{{.*}}.region_id = weak constant i8 0
// CHECK: @.offload_sizes = private unnamed_addr constant [2 x i64] [i64 4, i64 4]
// CHECK: @.offload_maptypes = private unnamed_addr constant [2 x i64] [i64 35, i64 289]
// CHECK: @.offload_mapnames = private constant [2 x ptr] [ptr @0, ptr @1]
// CHECK: @2 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @3 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @2 }, align 8
// CHECK: @.offloading.entry_name = internal unnamed_addr constant [{{.*}} x i8] c"__omp_offloading_{{.*}}_{{.*}}__QQmain_l{{.*}}\00", section ".llvm.rodata.offloading", align 1
// CHECK: @.offloading.entry.__omp_offloading_{{.*}}_{{.*}}__QQmain_l{{.*}} = weak constant %struct.__tgt_offload_entry {{[{]}} i64 0, i16 1, i16 1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}__QQmain_l{{.*}}.region_id, ptr @.offloading.entry_name, i64 0, i64 0, ptr null }, section "llvm_offload_entries", align 8
// CHECK-LABEL: define void @_QQmain(
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_1:.*]] = alloca [2 x ptr], align 8
// CHECK:   %[[VAL_2:.*]] = alloca [2 x ptr], align 8
// CHECK:   %[[VAL_3:.*]] = alloca [2 x ptr], align 8
// CHECK:   %[[VAL_4:.*]] = alloca %[[VAL_5:.*]], align 8
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_7:.*]] = load i32, ptr @_QFEi, align 4
// CHECK:   store i32 %[[VAL_7]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_8:.*]] = load i64, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_9:.*]] = getelementptr inbounds [2 x ptr], ptr %[[VAL_1]], i32 0, i32 0
// CHECK:   store ptr @_QFEsp, ptr %[[VAL_9]], align 8
// CHECK:   %[[VAL_10:.*]] = getelementptr inbounds [2 x ptr], ptr %[[VAL_2]], i32 0, i32 0
// CHECK:   store ptr @_QFEsp, ptr %[[VAL_10]], align 8
// CHECK:   %[[VAL_11:.*]] = getelementptr inbounds [2 x ptr], ptr %[[VAL_3]], i64 0, i64 0
// CHECK:   store ptr null, ptr %[[VAL_11]], align 8
// CHECK:   %[[VAL_12:.*]] = getelementptr inbounds [2 x ptr], ptr %[[VAL_1]], i32 0, i32 1
// CHECK:   store i64 %[[VAL_8]], ptr %[[VAL_12]], align 8
// CHECK:   %[[VAL_13:.*]] = getelementptr inbounds [2 x ptr], ptr %[[VAL_2]], i32 0, i32 1
// CHECK:   store i64 %[[VAL_8]], ptr %[[VAL_13]], align 8
// CHECK:   %[[VAL_14:.*]] = getelementptr inbounds [2 x ptr], ptr %[[VAL_3]], i64 0, i64 1
// CHECK:   store ptr null, ptr %[[VAL_14]], align 8
// CHECK:   %[[VAL_15:.*]] = getelementptr inbounds [2 x ptr], ptr %[[VAL_1]], i32 0, i32 0
// CHECK:   %[[VAL_16:.*]] = getelementptr inbounds [2 x ptr], ptr %[[VAL_2]], i32 0, i32 0
// CHECK:   %[[VAL_17:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 0
// CHECK:   store i32 3, ptr %[[VAL_17]], align 4
// CHECK:   %[[VAL_18:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 1
// CHECK:   store i32 2, ptr %[[VAL_18]], align 4
// CHECK:   %[[VAL_19:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 2
// CHECK:   store ptr %[[VAL_15]], ptr %[[VAL_19]], align 8
// CHECK:   %[[VAL_20:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 3
// CHECK:   store ptr %[[VAL_16]], ptr %[[VAL_20]], align 8
// CHECK:   %[[VAL_21:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 4
// CHECK:   store ptr @.offload_sizes, ptr %[[VAL_21]], align 8
// CHECK:   %[[VAL_22:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 5
// CHECK:   store ptr @.offload_maptypes, ptr %[[VAL_22]], align 8
// CHECK:   %[[VAL_23:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 6
// CHECK:   store ptr @.offload_mapnames, ptr %[[VAL_23]], align 8
// CHECK:   %[[VAL_24:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 7
// CHECK:   store ptr null, ptr %[[VAL_24]], align 8
// CHECK:   %[[VAL_25:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 8
// CHECK:   store i64 0, ptr %[[VAL_25]], align 8
// CHECK:   %[[VAL_26:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 9
// CHECK:   store i64 0, ptr %[[VAL_26]], align 8
// CHECK:   %[[VAL_27:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 10
// CHECK:   store [3 x i32] [i32 -1, i32 0, i32 0], ptr %[[VAL_27]], align 4
// CHECK:   %[[VAL_28:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 11
// CHECK:   store [3 x i32] zeroinitializer, ptr %[[VAL_28]], align 4
// CHECK:   %[[VAL_29:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 12
// CHECK:   store i32 0, ptr %[[VAL_29]], align 4
// CHECK:   %[[VAL_30:.*]] = call i32 @__tgt_target_kernel(ptr @3, i64 -1, i32 -1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}__QQmain_l{{.*}}.region_id, ptr %[[VAL_4]])
// CHECK:   %[[VAL_31:.*]] = icmp ne i32 %[[VAL_30]], 0
// CHECK:   br i1 %[[VAL_31]], label %[[VAL_32:.*]], label %[[VAL_33:.*]]
// CHECK: omp_offload.failed:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}__QQmain_l{{.*}}(ptr @_QFEsp, i64 %[[VAL_8]])
// CHECK:   br label %[[VAL_33]]
// CHECK: omp_offload.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}__QQmain_l{{.*}}(
// CHECK: entry:
// CHECK:   %[[VAL_0:.*]] = alloca i64, align 8
// CHECK:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_5:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   store i32 %[[VAL_5]], ptr %[[VAL_6:.*]], align 4
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
