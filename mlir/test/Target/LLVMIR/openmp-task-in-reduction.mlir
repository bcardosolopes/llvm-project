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

llvm.func @nested_task_in_reduction(%arg0: !llvm.ptr) {
  omp.taskgroup task_reduction(@add_i32 %arg0 -> %arg1 : !llvm.ptr) {
    omp.task in_reduction(@add_i32 %arg1 -> %arg2 : !llvm.ptr) {
      %one = llvm.mlir.constant(1 : i32) : i32
      %value = llvm.load %arg2 : !llvm.ptr -> i32
      %sum = llvm.add %value, %one : i32
      llvm.store %sum, %arg2 : i32, !llvm.ptr
      omp.terminator
    }
    omp.terminator
  }
  llvm.return
}

// CHECK-LABEL: define void @nested_task_in_reduction(ptr %{{.+}}) {
// CHECK: %[[DESC:.+]] = call ptr @__kmpc_taskred_init(i32 %{{.+}}, i32 1, ptr %.rd_input.)
// CHECK: store ptr %[[DESC]], ptr %[[DESC_SLOT:.+]], align 8
// CHECK: call i32 @__kmpc_omp_task(ptr @{{.+}}, i32 %{{.+}}, ptr %{{.+}})
//
// The thread id passed to __kmpc_task_reduction_get_th_data must be the
// executing task's own id, obtained via __kmpc_global_thread_num INSIDE the
// task body -- not a value captured from the task-creating thread. So the
// captured struct holds only the reduction descriptor + variable ({ ptr, ptr },
// no leading captured-i32-tid slot).
// CHECK-LABEL: define internal void @nested_task_in_reduction..omp_par(i32 %{{.+}}, ptr %{{.+}}) {
// CHECK: %[[SHARED:.+]] = load ptr, ptr %{{.+}}, align 8
// CHECK: %[[DESC_SLOT_ADDR:.+]] = getelementptr { ptr, ptr }, ptr %[[SHARED]], i32 0, i32 0
// CHECK: %[[DESC_VAL:.+]] = load ptr, ptr %[[DESC_SLOT_ADDR]], align 8
// CHECK: %[[RED_SLOT_ADDR:.+]] = getelementptr { ptr, ptr }, ptr %[[SHARED]], i32 0, i32 1
// CHECK: %[[RED:.+]] = load ptr, ptr %[[RED_SLOT_ADDR]], align 8
// CHECK: %[[TID:.+]] = call i32 @__kmpc_global_thread_num(ptr @{{.+}})
// CHECK: %[[ITEM:.+]] = call ptr @__kmpc_task_reduction_get_th_data(i32 %[[TID]], ptr %[[DESC_VAL]], ptr %[[RED]])
// CHECK: load i32, ptr %[[ITEM]], align 4
// CHECK: store i32 %{{.+}}, ptr %[[ITEM]], align 4

// -----

llvm.func @standalone_task_in_reduction(%arg0: !llvm.ptr) {
  omp.task in_reduction(@add_i32 %arg0 -> %arg1 : !llvm.ptr) {
    %one = llvm.mlir.constant(1 : i32) : i32
    %value = llvm.load %arg1 : !llvm.ptr -> i32
    %sum = llvm.add %value, %one : i32
    llvm.store %sum, %arg1 : i32, !llvm.ptr
    omp.terminator
  }
  llvm.return
}

// CHECK-LABEL: define void @standalone_task_in_reduction(ptr %{{.+}}) {
// CHECK: call i32 @__kmpc_omp_task(ptr @{{.*}}, i32 %{{.*}}, ptr %{{.*}})
//
// A standalone task (no enclosing taskgroup) has a null descriptor, so it
// captures only the reduction variable ({ ptr }, again no captured-i32-tid
// slot), and still derives the thread id inside the task body.
// CHECK-LABEL: define internal void @standalone_task_in_reduction..omp_par(i32 %{{.+}}, ptr %{{.+}}) {
// CHECK: %[[SHARED2:.+]] = load ptr, ptr %{{.+}}, align 8
// CHECK: %[[RED_SLOT_ADDR2:.+]] = getelementptr { ptr }, ptr %[[SHARED2]], i32 0, i32 0
// CHECK: %[[RED2:.+]] = load ptr, ptr %[[RED_SLOT_ADDR2]], align 8
// CHECK: %[[TID2:.+]] = call i32 @__kmpc_global_thread_num(ptr @{{.+}})
// CHECK: %[[ITEM2:.+]] = call ptr @__kmpc_task_reduction_get_th_data(i32 %[[TID2]], ptr null, ptr %[[RED2]])
// CHECK: load i32, ptr %[[ITEM2]], align 4
// CHECK: store i32 %{{.+}}, ptr %[[ITEM2]], align 4
