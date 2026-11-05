// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

module attributes {omp.is_target_device = false} {
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
// CHECK-LABEL: define void @omp_target_region_(
// CHECK:   %[[VAL_0:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, i64 1, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, i64 1, align 4
// CHECK:   store i32 10, ptr %[[VAL_0]], align 4
// CHECK:   store i32 20, ptr %[[VAL_1]], align 4
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: entry:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_omp_target_region__l{{.*}}(ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]])
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @omp_target_no_map(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_omp_target_no_map_l{{.*}}()
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
