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
    omp.target map_entries( %map1 -> %arg0, %map2 -> %arg1, %map3 -> %arg2 : !llvm.ptr, !llvm.ptr, !llvm.ptr) {
      omp.parallel {
        %8 = llvm.load %arg0 : !llvm.ptr -> i32
        %9 = llvm.load %arg1 : !llvm.ptr -> i32
        %10 = llvm.add %8, %9  : i32
        llvm.store %10, %arg2 : i32, !llvm.ptr
        omp.terminator
        }
      omp.terminator
    }
    llvm.return
  }
}

// CHECK: @0 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @1 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @2 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}};unknown;{{.*}};{{.*}};;\00", align 1
// CHECK: @3 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @4 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @3 }, align 8
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
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_omp_target_region__l{{.*}}(
// CHECK: entry:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @4)
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @4, i32 3, ptr @__omp_offloading_{{.*}}_{{.*}}_omp_target_region__l{{.*}}..omp_par, ptr %[[VAL_5:.*]], ptr %[[VAL_6:.*]], ptr %[[VAL_7:.*]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_omp_target_region__l{{.*}}..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region3:
// CHECK:   %[[VAL_7:.*]] = load i32, ptr %[[VAL_8:.*]], align 4
// CHECK:   %[[VAL_9:.*]] = load i32, ptr %[[VAL_10:.*]], align 4
// CHECK:   %[[VAL_11:.*]] = add i32 %[[VAL_7]], %[[VAL_9]]
// CHECK:   store i32 %[[VAL_11]], ptr %[[VAL_12:.*]], align 4
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
