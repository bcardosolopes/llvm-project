// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s

llvm.func @foo(!llvm.ptr) -> ()

omp.private {type = private} @i32_priv : i32

llvm.func @parallel_allocate_private(%arg0 : !llvm.ptr) {
  %allocator = llvm.mlir.constant(7 : i32) : i32
  omp.parallel allocate(%allocator : i32 -> %arg0 : !llvm.ptr)
               private(@i32_priv %arg0 -> %arg2 : !llvm.ptr) {
    llvm.call @foo(%arg2) : (!llvm.ptr) -> ()
    omp.terminator
  }
  llvm.return
}

// CHECK-LABEL: define void @parallel_allocate_private(
// CHECK:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// CHECK-LABEL: define internal void @parallel_allocate_private..omp_par(
// CHECK:         %[[ALLOC_TID:.+]] = call i32 @__kmpc_global_thread_num(
// CHECK:         %[[PRIVATE_ALLOC:.+]] = call ptr @__kmpc_alloc(i32 %[[ALLOC_TID]], i64 4, ptr inttoptr (i32 7 to ptr))
// CHECK:         call void @foo(ptr %[[PRIVATE_ALLOC]])
// CHECK:         %[[FREE_TID:.+]] = call i32 @__kmpc_global_thread_num(
// CHECK:         call void @__kmpc_free(i32 %[[FREE_TID]], ptr %[[PRIVATE_ALLOC]], ptr inttoptr (i32 7 to ptr))
// CHECK:         ret void

// -----

llvm.func @foo(!llvm.ptr) -> ()
omp.private {type = private} @i32_priv : i32

llvm.func @wsloop_allocate_private(%arg0 : !llvm.ptr) {
  %allocator = llvm.mlir.constant(5 : i32) : i32
  %c0 = llvm.mlir.constant(0 : i32) : i32
  %c4 = llvm.mlir.constant(4 : i32) : i32
  %c1 = llvm.mlir.constant(1 : i32) : i32
  omp.parallel {
    omp.wsloop allocate(%allocator : i32 -> %arg0 : !llvm.ptr)
               private(@i32_priv %arg0 -> %arg1 : !llvm.ptr) {
      omp.loop_nest (%iv) : i32 = (%c0) to (%c4) step (%c1) {
        llvm.call @foo(%arg1) : (!llvm.ptr) -> ()
        omp.yield
      }
    }
    omp.terminator
  }
  llvm.return
}

// CHECK-LABEL: define void @wsloop_allocate_private(
// CHECK:         call void (ptr, i32, ptr, ...) @__kmpc_fork_call(
// CHECK-LABEL: define internal void @wsloop_allocate_private..omp_par(
// CHECK:         %[[ALLOC_TID:.+]] = call i32 @__kmpc_global_thread_num(
// CHECK:         %[[PRIVATE_ALLOC:.+]] = call ptr @__kmpc_alloc(i32 %[[ALLOC_TID]], i64 4, ptr inttoptr (i32 5 to ptr))
// CHECK:         call void @__kmpc_for_static_init_4u(
// CHECK-DAG:     call void @foo(ptr %[[PRIVATE_ALLOC]])
// CHECK-DAG:     call void @__kmpc_for_static_fini(
// CHECK-DAG:     call void @__kmpc_barrier(
// CHECK-DAG:     call void @__kmpc_free(i32 %{{.*}}, ptr %[[PRIVATE_ALLOC]], ptr inttoptr (i32 5 to ptr))
// CHECK:         ret void
