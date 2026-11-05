// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

omp.declare_reduction @add_i32 : i32 init {
^bb0(%arg0: i32):
  %zero = llvm.mlir.constant(0 : i32) : i32
  omp.yield(%zero : i32)
} combiner {
^bb0(%arg0: i32, %arg1: i32):
  %sum = llvm.add %arg0, %arg1 : i32
  omp.yield(%sum : i32)
}

llvm.func @taskloop_reduction(%lb : i32, %ub : i32, %step : i32, %arg0: !llvm.ptr) {
  omp.taskloop reduction(@add_i32 %arg0 -> %arg1 : !llvm.ptr) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      %one = llvm.mlir.constant(1 : i32) : i32
      %value = llvm.load %arg1 : !llvm.ptr -> i32
      %sum = llvm.add %value, %one : i32
      llvm.store %sum, %arg1 : i32, !llvm.ptr
      omp.yield
    }
  }
  llvm.return
}

// CHECK-LABEL: define void @taskloop_reduction(i32 %{{.+}}, i32 %{{.+}}, i32 %{{.+}}, ptr %{{.+}}) {
// CHECK: call void @__kmpc_taskgroup(ptr @{{.+}}, i32 %[[TASKGROUP_TID:.+]])
// CHECK: %[[DESC:.+]] = call ptr @__kmpc_taskred_init(i32 %[[TASKGROUP_TID]], i32 1, ptr %.rd_input.)
// CHECK: call void @__kmpc_taskloop(ptr @{{.+}}, i32 %{{.+}}, ptr %{{.+}}, i32 1, ptr %{{.+}}, ptr %{{.+}}, i64 %{{.+}}, i32 1, i32 0, i64 0, ptr @omp_taskloop_dup{{.*}})
// CHECK: call void @__kmpc_end_taskgroup(ptr @{{.+}}, i32 %[[TASKGROUP_TID]])
//
// CHECK-LABEL: define internal void @taskloop_reduction..omp_par(i32 %{{.+}}, ptr %{{.+}}) {
// CHECK: %[[CTX_SLOT:.+]] = getelementptr {{.*}}, ptr %{{.+}}, i32 0, i32 3
// CHECK: %[[CTX:.+]] = load ptr, ptr %[[CTX_SLOT]], align 8
// CHECK: %[[DESC_SLOT:.+]] = getelementptr { ptr, ptr }, ptr %[[CTX]], i32 0, i32 0
// CHECK: %[[RED_SLOT:.+]] = getelementptr { ptr, ptr }, ptr %[[CTX]], i32 0, i32 1
// CHECK: %[[DESC_VAL:.+]] = load ptr, ptr %[[DESC_SLOT]], align 8
// CHECK: %[[RED:.+]] = load ptr, ptr %[[RED_SLOT]], align 8
// CHECK: %[[ITEM:.+]] = call ptr @__kmpc_task_reduction_get_th_data(i32 %{{.+}}, ptr %[[DESC_VAL]], ptr %[[RED]])
// CHECK: load i32, ptr %[[ITEM]], align 4
// CHECK: store i32 %{{.+}}, ptr %[[ITEM]], align 4

// -----

llvm.func @taskloop_in_reduction(%lb : i32, %ub : i32, %step : i32, %arg0: !llvm.ptr) {
  omp.taskgroup task_reduction(@add_i32 %arg0 -> %arg1 : !llvm.ptr) {
    omp.taskloop nogroup in_reduction(@add_i32 %arg1 -> %arg2 : !llvm.ptr) {
      omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
        %one = llvm.mlir.constant(1 : i32) : i32
        %value = llvm.load %arg2 : !llvm.ptr -> i32
        %sum = llvm.add %value, %one : i32
        llvm.store %sum, %arg2 : i32, !llvm.ptr
        omp.yield
      }
    }
    omp.terminator
  }
  llvm.return
}

// CHECK-LABEL: define void @taskloop_in_reduction(i32 %{{.+}}, i32 %{{.+}}, i32 %{{.+}}, ptr %{{.+}}) {
// CHECK: %[[OUTER_DESC:.+]] = call ptr @__kmpc_taskred_init(i32 %{{.+}}, i32 1, ptr %.rd_input.
// CHECK: call void @__kmpc_end_taskgroup(ptr @{{.+}}, i32 %{{.+}})
// CHECK: call void @__kmpc_taskloop(ptr @{{.+}}, i32 %{{.+}}, ptr %{{.+}}, i32 1, ptr %{{.+}}, ptr %{{.+}}, i64 %{{.+}}, i32 1, i32 0, i64 0, ptr @omp_taskloop_dup{{.*}})
//
// CHECK-LABEL: define internal void @taskloop_in_reduction..omp_par(i32 %{{.+}}, ptr %{{.+}}) {
// CHECK: %[[CTX_SLOT2:.+]] = getelementptr {{.*}}, ptr %{{.+}}, i32 0, i32 3
// CHECK: %[[CTX2:.+]] = load ptr, ptr %[[CTX_SLOT2]], align 8
// CHECK: %[[DESC_SLOT2:.+]] = getelementptr { ptr, ptr }, ptr %[[CTX2]], i32 0, i32 0
// CHECK: %[[RED_SLOT2:.+]] = getelementptr { ptr, ptr }, ptr %[[CTX2]], i32 0, i32 1
// CHECK: %[[DESC_VAL2:.+]] = load ptr, ptr %[[DESC_SLOT2]], align 8
// CHECK: %[[RED2:.+]] = load ptr, ptr %[[RED_SLOT2]], align 8
// CHECK: %[[ITEM2:.+]] = call ptr @__kmpc_task_reduction_get_th_data(i32 %{{.+}}, ptr %[[DESC_VAL2]], ptr %[[RED2]])
// CHECK: load i32, ptr %[[ITEM2]], align 4
// CHECK: store i32 %{{.+}}, ptr %[[ITEM2]], align 4
