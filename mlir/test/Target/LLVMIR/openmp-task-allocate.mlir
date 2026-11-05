// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

llvm.func @foo(!llvm.ptr) -> ()
llvm.func @destroy(!llvm.ptr) -> ()

omp.private {type = firstprivate} @privatizer : i32 copy {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %0 = llvm.load %arg0 : !llvm.ptr -> i32
  llvm.store %0, %arg1 : i32, !llvm.ptr
  omp.yield(%arg1 : !llvm.ptr)
} dealloc {
^bb0(%arg0 : !llvm.ptr):
  llvm.call @destroy(%arg0) : (!llvm.ptr) -> ()
  omp.yield
}

llvm.func @task_allocate(%arg0 : !llvm.ptr) {
  %allocator = llvm.mlir.constant(7 : i32) : i32
  omp.task allocate(%allocator : i32 -> %arg0 : !llvm.ptr)
           private(@privatizer %arg0 -> %arg1 : !llvm.ptr) {
    llvm.call @foo(%arg1) : (!llvm.ptr) -> ()
    omp.terminator
  }
  llvm.return
}

// CHECK-LABEL: define void @task_allocate(ptr
// CHECK:         %[[STRUCT_ARG:.*]] = alloca { ptr }, align 8
// CHECK-NOT:     @malloc(
// CHECK:         br label %omp.private.init
// CHECK:       omp.private.init:
// CHECK:         %[[ALLOC_TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.+}})
// CHECK:         %[[PRIVATE_ALLOC:.*]] = call ptr @__kmpc_alloc(i32 %[[ALLOC_TID]], i64 4, ptr inttoptr (i32 7 to ptr))
// CHECK:         br label %omp.private.copy
// CHECK:       omp.private.copy:
// CHECK:         %[[LOADED:.*]] = load i32, ptr %{{.*}}, align 4
// CHECK:         store i32 %[[LOADED]], ptr %[[PRIVATE_ALLOC]], align 4
// CHECK:         br label %omp.task.start
// CHECK:       omp.task.start:
// CHECK:         %[[TASK_ALLOC:.*]] = call ptr @__kmpc_omp_task_alloc
// CHECK:         call i32 @__kmpc_omp_task(ptr @{{.+}}, i32 %{{.+}}, ptr %[[TASK_ALLOC]])
//
// CHECK-LABEL: define internal void @task_allocate..omp_par
// CHECK:         %[[SHAREDS:.*]] = load ptr, ptr %[[STRUCT_ARG:.*]], align 8
// CHECK:         %[[CAPTURE_GEP:.*]] = getelementptr { ptr }, ptr %[[SHAREDS]], i32 0, i32 0
// CHECK:         %[[ALLOCATED_PTR:.*]] = load ptr, ptr %[[CAPTURE_GEP]], align 8
// CHECK:         br label %task.body
// CHECK:       task.body:
// CHECK:         br label %omp.task.region
// CHECK:       omp.task.region:
// CHECK:         call void @foo(ptr %[[ALLOCATED_PTR]])
// CHECK:         br label %omp.region.cont
// CHECK:       omp.region.cont:
// CHECK:         call void @destroy(ptr %[[ALLOCATED_PTR]])
// CHECK:         %[[FREE_TID:.*]] = call i32 @__kmpc_global_thread_num(ptr @{{.+}})
// CHECK:         call void @__kmpc_free(i32 %[[FREE_TID]], ptr %[[ALLOCATED_PTR]], ptr inttoptr (i32 7 to ptr))
// CHECK:         br label %task.exit.exitStub
