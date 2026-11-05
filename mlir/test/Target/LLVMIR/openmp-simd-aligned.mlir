// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s

llvm.func @_QPsimd_aligned_pointer() {
  %1 = llvm.mlir.constant(1 : i64) : i64
  %2 = llvm.alloca %1 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8)> {bindc_name = "x"} : (i64) -> !llvm.ptr
  %3 = llvm.alloca %1 x i32 {bindc_name = "i", pinned} : (i64) -> !llvm.ptr
  %4 = llvm.mlir.constant(1 : i32) : i32
  %5 = llvm.mlir.constant(10 : i32) : i32
  %6 = llvm.mlir.constant(1 : i32) : i32
  omp.simd aligned(%2 : !llvm.ptr -> 256 : i64) {
    omp.loop_nest (%arg0) : i32 = (%4) to (%5) inclusive step (%6) {
      llvm.store %arg0, %3 : i32, !llvm.ptr
      omp.yield
    }
  }
  llvm.return
}

llvm.func @_QPsimd_aligned_cptr() {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x !llvm.struct<"_QM__fortran_builtinsT__builtin_c_ptr", (i64)> {bindc_name = "a"} : (i64) -> !llvm.ptr
  %2 = llvm.mlir.constant(1 : i64) : i64
  %3 = llvm.alloca %2 x i32 {bindc_name = "i", pinned} : (i64) -> !llvm.ptr
  %4 = llvm.mlir.constant(1 : i32) : i32
  %5 = llvm.mlir.constant(10 : i32) : i32
  %6 = llvm.mlir.constant(1 : i32) : i32
  omp.simd aligned(%1 : !llvm.ptr -> 256 : i64) {
    omp.loop_nest (%arg0) : i32 = (%4) to (%5) inclusive step (%6) {
      llvm.store %arg0, %3 : i32, !llvm.ptr
      omp.yield
    }
  }
  llvm.return
}

llvm.func @_QPsimd_aligned_allocatable() {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8, array<1 x array<3 x i64>>)> {bindc_name = "a"} : (i64) -> !llvm.ptr
  %2 = llvm.mlir.constant(1 : i32) : i32
  %3 = llvm.mlir.constant(10 : i32) : i32
  %4 = llvm.mlir.constant(1 : i32) : i32
  omp.simd aligned(%1 : !llvm.ptr -> 256 : i64) {
    omp.loop_nest (%arg0) : i32 = (%2) to (%3) inclusive step (%4) {
      omp.yield
    }
  }
  llvm.return
}

llvm.func @_QPsimd_aligned_non_power_of_two() {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.alloca %0 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8, array<1 x array<3 x i64>>)> {bindc_name = "a"} : (i64) -> !llvm.ptr
  %2 = llvm.alloca %0 x !llvm.struct<(ptr, i64, i32, i8, i8, i8, i8, array<1 x array<3 x i64>>)> {bindc_name = "b"} : (i64) -> !llvm.ptr
  %3 = llvm.mlir.constant(1 : i32) : i32
  %4 = llvm.mlir.constant(10 : i32) : i32
  %5 = llvm.mlir.constant(1 : i32) : i32
  omp.simd aligned(%1 : !llvm.ptr -> 257 : i64, %2 : !llvm.ptr -> 64 : i64) {
    omp.loop_nest (%arg0) : i32 = (%3) to (%4) inclusive step (%5) {
      omp.yield
    }
  }
  llvm.return
}

// CHECK-LABEL: define void @_QPsimd_aligned_pointer(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8 }, i64 1, align 8
// CHECK:   %[[VAL_1:.*]] = alloca i32, i64 1, align 4
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.simd.region:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   call void @llvm.assume(i1 true) [ "align"(ptr %[[VAL_0]], i64 256) ]
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_7:.*]] = phi i32 [ 0, %[[VAL_5]] ], [ %[[VAL_8:.*]], %[[VAL_9:.*]] ]
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_11:.*]] = icmp ult i32 %[[VAL_7]], 10
// CHECK:   br i1 %[[VAL_11]], label %[[VAL_12:.*]], label %[[VAL_13:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_14:.*]] = mul i32 %[[VAL_7]], 1
// CHECK:   %[[VAL_15:.*]] = add i32 %[[VAL_14]], 1
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_15]], ptr %[[VAL_1]], align 4, !llvm.access.group !1
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_9]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_8]] = add nuw i32 %[[VAL_7]], 1
// CHECK:   br label %[[VAL_6]], !llvm.loop !2
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPsimd_aligned_cptr(
// CHECK:   %[[VAL_0:.*]] = alloca %[[VAL_1:.*]], i64 1, align 8
// CHECK:   %[[VAL_2:.*]] = alloca i32, i64 1, align 4
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.simd.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   call void @llvm.assume(i1 true) [ "align"(ptr %[[VAL_0]], i64 256) ]
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_8:.*]] = phi i32 [ 0, %[[VAL_6]] ], [ %[[VAL_9:.*]], %[[VAL_10:.*]] ]
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_12:.*]] = icmp ult i32 %[[VAL_8]], 10
// CHECK:   br i1 %[[VAL_12]], label %[[VAL_13:.*]], label %[[VAL_14:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_15:.*]] = mul i32 %[[VAL_8]], 1
// CHECK:   %[[VAL_16:.*]] = add i32 %[[VAL_15]], 1
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   store i32 %[[VAL_16]], ptr %[[VAL_2]], align 4, !llvm.access.group !5
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_10]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_9]] = add nuw i32 %[[VAL_8]], 1
// CHECK:   br label %[[VAL_7]], !llvm.loop !6
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPsimd_aligned_allocatable(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8, [1 x [3 x i64]] }, i64 1, align 8
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.simd.region:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   call void @llvm.assume(i1 true) [ "align"(ptr %[[VAL_0]], i64 256) ]
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_6:.*]] = phi i32 [ 0, %[[VAL_4]] ], [ %[[VAL_7:.*]], %[[VAL_8:.*]] ]
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_10:.*]] = icmp ult i32 %[[VAL_6]], 10
// CHECK:   br i1 %[[VAL_10]], label %[[VAL_11:.*]], label %[[VAL_12:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_13:.*]] = mul i32 %[[VAL_6]], 1
// CHECK:   %[[VAL_14:.*]] = add i32 %[[VAL_13]], 1
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_8]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_7]] = add nuw i32 %[[VAL_6]], 1
// CHECK:   br label %[[VAL_5]], !llvm.loop !8
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @_QPsimd_aligned_non_power_of_two(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8, [1 x [3 x i64]] }, i64 1, align 8
// CHECK:   %[[VAL_1:.*]] = alloca {{[{]}} ptr, i64, i32, i8, i8, i8, i8, [1 x [3 x i64]] }, i64 1, align 8
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.simd.region:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   call void @llvm.assume(i1 true) [ "align"(ptr %[[VAL_1]], i64 64) ]
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_7:.*]] = phi i32 [ 0, %[[VAL_5]] ], [ %[[VAL_8:.*]], %[[VAL_9:.*]] ]
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_11:.*]] = icmp ult i32 %[[VAL_7]], 10
// CHECK:   br i1 %[[VAL_11]], label %[[VAL_12:.*]], label %[[VAL_13:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_14:.*]] = mul i32 %[[VAL_7]], 1
// CHECK:   %[[VAL_15:.*]] = add i32 %[[VAL_14]], 1
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_9]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_8]] = add nuw i32 %[[VAL_7]], 1
// CHECK:   br label %[[VAL_6]], !llvm.loop !11
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
