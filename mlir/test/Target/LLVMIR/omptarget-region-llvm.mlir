// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

module attributes {omp.is_target_device = false, omp.target_triples = ["amdgcn-amd-amdhsa"]} {
  llvm.func @omp_target_region_() {
    %0 = llvm.mlir.constant(20 : i32) : i32
    %1 = llvm.mlir.constant(10 : i32) : i32
    %2 = llvm.mlir.constant(1 : i64) : i64
    %3 = llvm.alloca %2 x i32 {bindc_name = "a", in_type = i32, operandSegmentSizes = array<i32: 0, 0>, uniq_name = "_QFomp_target_regionEa"} : (i64) -> !llvm.ptr
    %4 = llvm.mlir.constant(1 : i64) : i64
    %5 = llvm.alloca %4 x i32 {bindc_name = "b", in_type = i32, operandSegmentSizes = array<i32: 0, 0>, uniq_name = "_QFomp_target_regionEb"} : (i64) -> !llvm.ptr
    %6 = llvm.mlir.constant(1 : i64) : i64
    %7 = llvm.alloca %6 x i32 {bindc_name = "c", in_type = i32, operandSegmentSizes = array<i32: 0, 0>, uniq_name = "_QFomp_target_regionEc"} : (i64) -> !llvm.ptr
    llvm.store %1, %3 : i32, !llvm.ptr
    llvm.store %0, %5 : i32, !llvm.ptr
    %map1 = omp.map.info var_ptr(%3 : !llvm.ptr, i32)   map_clauses(tofrom) capture(ByRef) -> !llvm.ptr {name = ""}
    %map2 = omp.map.info var_ptr(%5 : !llvm.ptr, i32)   map_clauses(tofrom) capture(ByRef) -> !llvm.ptr {name = ""}
    %map3 = omp.map.info var_ptr(%7 : !llvm.ptr, i32)   map_clauses(tofrom) capture(ByRef) -> !llvm.ptr {name = ""}
    omp.target map_entries(%map1 -> %arg0, %map2 -> %arg1, %map3 -> %arg2 : !llvm.ptr, !llvm.ptr, !llvm.ptr) {
      %8 = llvm.load %arg0 : !llvm.ptr -> i32
      %9 = llvm.load %arg1 : !llvm.ptr -> i32
      %10 = llvm.add %8, %9  : i32
      llvm.store %10, %arg2 : i32, !llvm.ptr
      omp.terminator
    }
    llvm.return
  }

  llvm.func @omp_target_no_map() {
    omp.target {
      omp.terminator
    }
    llvm.return
  }
}

// CHECK: @0 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @1 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @2 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @.__omp_offloading_{{.*}}_{{.*}}_omp_target_region__l{{.*}}.region_id = weak constant i8 0
// CHECK: @.offload_sizes = private unnamed_addr constant [3 x i64] [i64 4, i64 4, i64 4]
// CHECK: @.offload_maptypes = private unnamed_addr constant [3 x i64] [i64 35, i64 35, i64 35]
// CHECK: @.offload_mapnames = private constant [3 x ptr] [ptr @0, ptr @1, ptr @2]
// CHECK: @3 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @4 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @3 }, align 8
// CHECK: @.__omp_offloading_{{.*}}_{{.*}}_omp_target_no_map_l{{.*}}.region_id = weak constant i8 0
// CHECK: @.offloading.entry_name = internal unnamed_addr constant [{{.*}} x i8] c"__omp_offloading_{{.*}}_{{.*}}_omp_target_region__l{{.*}}\00", section ".llvm.rodata.offloading", align 1
// CHECK: @.offloading.entry.__omp_offloading_{{.*}}_{{.*}}_omp_target_region__l{{.*}} = weak constant %struct.__tgt_offload_entry {{[{]}} i64 0, i16 1, i16 1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}_omp_target_region__l{{.*}}.region_id, ptr @.offloading.entry_name, i64 0, i64 0, ptr null }, section "llvm_offload_entries", align 8
// CHECK: @.offloading.entry_name.1 = internal unnamed_addr constant [{{.*}} x i8] c"__omp_offloading_{{.*}}_{{.*}}_omp_target_no_map_l{{.*}}\00", section ".llvm.rodata.offloading", align 1
// CHECK: @.offloading.entry.__omp_offloading_{{.*}}_{{.*}}_omp_target_no_map_l{{.*}} = weak constant %struct.__tgt_offload_entry {{[{]}} i64 0, i16 1, i16 1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}_omp_target_no_map_l{{.*}}.region_id, ptr @.offloading.entry_name.1, i64 0, i64 0, ptr null }, section "llvm_offload_entries", align 8
// CHECK-LABEL: define void @omp_target_region_(
// CHECK:   %[[VAL_0:.*]] = alloca [3 x ptr], align 8
// CHECK:   %[[VAL_1:.*]] = alloca [3 x ptr], align 8
// CHECK:   %[[VAL_2:.*]] = alloca [3 x ptr], align 8
// CHECK:   %[[VAL_3:.*]] = alloca %[[VAL_4:.*]], align 8
// CHECK:   %[[VAL_5:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, i64 1, align 4
// CHECK:   store i32 10, ptr %[[VAL_5]], align 4
// CHECK:   store i32 20, ptr %[[VAL_6]], align 4
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_9:.*]] = getelementptr inbounds [3 x ptr], ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_5]], ptr %[[VAL_9]], align 8
// CHECK:   %[[VAL_10:.*]] = getelementptr inbounds [3 x ptr], ptr %[[VAL_1]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_5]], ptr %[[VAL_10]], align 8
// CHECK:   %[[VAL_11:.*]] = getelementptr inbounds [3 x ptr], ptr %[[VAL_2]], i64 0, i64 0
// CHECK:   store ptr null, ptr %[[VAL_11]], align 8
// CHECK:   %[[VAL_12:.*]] = getelementptr inbounds [3 x ptr], ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   store ptr %[[VAL_6]], ptr %[[VAL_12]], align 8
// CHECK:   %[[VAL_13:.*]] = getelementptr inbounds [3 x ptr], ptr %[[VAL_1]], i32 0, i32 1
// CHECK:   store ptr %[[VAL_6]], ptr %[[VAL_13]], align 8
// CHECK:   %[[VAL_14:.*]] = getelementptr inbounds [3 x ptr], ptr %[[VAL_2]], i64 0, i64 1
// CHECK:   store ptr null, ptr %[[VAL_14]], align 8
// CHECK:   %[[VAL_15:.*]] = getelementptr inbounds [3 x ptr], ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   store ptr %[[VAL_7]], ptr %[[VAL_15]], align 8
// CHECK:   %[[VAL_16:.*]] = getelementptr inbounds [3 x ptr], ptr %[[VAL_1]], i32 0, i32 2
// CHECK:   store ptr %[[VAL_7]], ptr %[[VAL_16]], align 8
// CHECK:   %[[VAL_17:.*]] = getelementptr inbounds [3 x ptr], ptr %[[VAL_2]], i64 0, i64 2
// CHECK:   store ptr null, ptr %[[VAL_17]], align 8
// CHECK:   %[[VAL_18:.*]] = getelementptr inbounds [3 x ptr], ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   %[[VAL_19:.*]] = getelementptr inbounds [3 x ptr], ptr %[[VAL_1]], i32 0, i32 0
// CHECK:   %[[VAL_20:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 0
// CHECK:   store i32 3, ptr %[[VAL_20]], align 4
// CHECK:   %[[VAL_21:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 1
// CHECK:   store i32 3, ptr %[[VAL_21]], align 4
// CHECK:   %[[VAL_22:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 2
// CHECK:   store ptr %[[VAL_18]], ptr %[[VAL_22]], align 8
// CHECK:   %[[VAL_23:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 3
// CHECK:   store ptr %[[VAL_19]], ptr %[[VAL_23]], align 8
// CHECK:   %[[VAL_24:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 4
// CHECK:   store ptr @.offload_sizes, ptr %[[VAL_24]], align 8
// CHECK:   %[[VAL_25:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 5
// CHECK:   store ptr @.offload_maptypes, ptr %[[VAL_25]], align 8
// CHECK:   %[[VAL_26:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 6
// CHECK:   store ptr @.offload_mapnames, ptr %[[VAL_26]], align 8
// CHECK:   %[[VAL_27:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 7
// CHECK:   store ptr null, ptr %[[VAL_27]], align 8
// CHECK:   %[[VAL_28:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 8
// CHECK:   store i64 0, ptr %[[VAL_28]], align 8
// CHECK:   %[[VAL_29:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 9
// CHECK:   store i64 0, ptr %[[VAL_29]], align 8
// CHECK:   %[[VAL_30:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 10
// CHECK:   store [3 x i32] [i32 -1, i32 0, i32 0], ptr %[[VAL_30]], align 4
// CHECK:   %[[VAL_31:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 11
// CHECK:   store [3 x i32] zeroinitializer, ptr %[[VAL_31]], align 4
// CHECK:   %[[VAL_32:.*]] = getelementptr inbounds nuw %[[VAL_4]], ptr %[[VAL_3]], i32 0, i32 12
// CHECK:   store i32 0, ptr %[[VAL_32]], align 4
// CHECK:   %[[VAL_33:.*]] = call i32 @__tgt_target_kernel(ptr @4, i64 -1, i32 -1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}_omp_target_region__l{{.*}}.region_id, ptr %[[VAL_3]])
// CHECK:   %[[VAL_34:.*]] = icmp ne i32 %[[VAL_33]], 0
// CHECK:   br i1 %[[VAL_34]], label %[[VAL_35:.*]], label %[[VAL_36:.*]]
// CHECK: omp_offload.failed:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_omp_target_region__l{{.*}}(ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]])
// CHECK:   br label %[[VAL_36]]
// CHECK: omp_offload.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @omp_target_no_map(
// CHECK:   %[[VAL_0:.*]] = alloca %[[VAL_1:.*]], align 8
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_3:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store i32 3, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_4:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   store i32 0, ptr %[[VAL_4]], align 4
// CHECK:   %[[VAL_5:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   store ptr null, ptr %[[VAL_5]], align 8
// CHECK:   %[[VAL_6:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 3
// CHECK:   store ptr null, ptr %[[VAL_6]], align 8
// CHECK:   %[[VAL_7:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 4
// CHECK:   store ptr null, ptr %[[VAL_7]], align 8
// CHECK:   %[[VAL_8:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 5
// CHECK:   store ptr null, ptr %[[VAL_8]], align 8
// CHECK:   %[[VAL_9:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 6
// CHECK:   store ptr null, ptr %[[VAL_9]], align 8
// CHECK:   %[[VAL_10:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 7
// CHECK:   store ptr null, ptr %[[VAL_10]], align 8
// CHECK:   %[[VAL_11:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 8
// CHECK:   store i64 0, ptr %[[VAL_11]], align 8
// CHECK:   %[[VAL_12:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 9
// CHECK:   store i64 0, ptr %[[VAL_12]], align 8
// CHECK:   %[[VAL_13:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 10
// CHECK:   store [3 x i32] [i32 -1, i32 0, i32 0], ptr %[[VAL_13]], align 4
// CHECK:   %[[VAL_14:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 11
// CHECK:   store [3 x i32] zeroinitializer, ptr %[[VAL_14]], align 4
// CHECK:   %[[VAL_15:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 12
// CHECK:   store i32 0, ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_16:.*]] = call i32 @__tgt_target_kernel(ptr @4, i64 -1, i32 -1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}_omp_target_no_map_l{{.*}}.region_id, ptr %[[VAL_0]])
// CHECK:   %[[VAL_17:.*]] = icmp ne i32 %[[VAL_16]], 0
// CHECK:   br i1 %[[VAL_17]], label %[[VAL_18:.*]], label %[[VAL_19:.*]]
// CHECK: omp_offload.failed:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_omp_target_no_map_l{{.*}}()
// CHECK:   br label %[[VAL_19]]
// CHECK: omp_offload.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_omp_target_region__l{{.*}}(
// CHECK: entry:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_4:.*]], align 4
// CHECK:   %[[VAL_5:.*]] = load i32, ptr %[[VAL_6:.*]], align 4
// CHECK:   %[[VAL_7:.*]] = add i32 %[[VAL_3]], %[[VAL_5]]
// CHECK:   store i32 %[[VAL_7]], ptr %[[VAL_8:.*]], align 4
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_omp_target_no_map_l{{.*}}(
// CHECK: entry:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.target:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
