// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s

llvm.func @distribute_order(%lb : i32, %ub : i32, %step : i32) {
  omp.distribute order(concurrent) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.yield
    }
  }
  llvm.return
}

// CHECK-LABEL: define void @distribute_order(
// CHECK:         %[[LASTITER:.*]] = alloca i32
// CHECK:         %[[LB:.*]] = alloca i32
// CHECK:         %[[UB:.*]] = alloca i32
// CHECK:         %[[STRIDE:.*]] = alloca i32
// CHECK:         %[[TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// CHECK:         call void @__kmpc_for_static_init_{{.*}}(ptr @{{.*}}, i32 %[[TID]], i32 92, ptr %[[LASTITER]], ptr %[[LB]], ptr %[[UB]], ptr %[[STRIDE]], i32 1, i32 1)

// -----

llvm.func @distribute_order_reproducible(%lb : i32, %ub : i32, %step : i32) {
  omp.distribute order(reproducible:concurrent) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.yield
    }
  }
  llvm.return
}

// CHECK-LABEL: define void @distribute_order_reproducible(
// CHECK:         %[[LASTITER:.*]] = alloca i32
// CHECK:         %[[LB:.*]] = alloca i32
// CHECK:         %[[UB:.*]] = alloca i32
// CHECK:         %[[STRIDE:.*]] = alloca i32
// CHECK:         %[[TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// CHECK:         call void @__kmpc_for_static_init_{{.*}}(ptr @{{.*}}, i32 %[[TID]], i32 92, ptr %[[LASTITER]], ptr %[[LB]], ptr %[[UB]], ptr %[[STRIDE]], i32 1, i32 1)

// -----

llvm.func @distribute_order_unconstrained(%lb : i32, %ub : i32, %step : i32) {
  omp.distribute order(unconstrained:concurrent) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.yield
    }
  }
  llvm.return
}

// CHECK-LABEL: define void @distribute_order_unconstrained(
// CHECK:         %[[LASTITER:.*]] = alloca i32
// CHECK:         %[[LB:.*]] = alloca i32
// CHECK:         %[[UB:.*]] = alloca i32
// CHECK:         %[[STRIDE:.*]] = alloca i32
// CHECK:         %[[TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.*}})
// CHECK:         call void @__kmpc_for_static_init_{{.*}}(ptr @{{.*}}, i32 %[[TID]], i32 92, ptr %[[LASTITER]], ptr %[[LB]], ptr %[[UB]], ptr %[[STRIDE]], i32 1, i32 1)
