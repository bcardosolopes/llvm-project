// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

omp.declare_reduction @add_reduction_i_32 : !llvm.ptr attributes {byref_element_type = !llvm.array<4 x i32>} alloc {
  %c1 = llvm.mlir.constant(1 : i64) : i64
  %alloc = llvm.alloca %c1 x !llvm.array<4 x i32> : (i64) -> !llvm.ptr
  omp.yield(%alloc : !llvm.ptr)
} init {
^bb0(%orig: !llvm.ptr, %alloc: !llvm.ptr):
  %zero = llvm.mlir.constant(0 : i32) : i32
  %base = llvm.getelementptr %alloc[0, 0] : (!llvm.ptr) -> !llvm.ptr, !llvm.array<4 x i32>
  %end = llvm.getelementptr %alloc[0, 4] : (!llvm.ptr) -> !llvm.ptr, !llvm.array<4 x i32>
  llvm.br ^bb1(%base : !llvm.ptr)
^bb1(%it: !llvm.ptr):  // 2 preds: ^bb0, ^bb2
  %done = llvm.icmp "eq" %it, %end : !llvm.ptr
  llvm.cond_br %done, ^bb3, ^bb2
^bb2:
  llvm.store %zero, %it : i32, !llvm.ptr
  %next = llvm.getelementptr %it[1] : (!llvm.ptr) -> !llvm.ptr, i32
  llvm.br ^bb1(%next : !llvm.ptr)
^bb3:
  omp.yield(%alloc : !llvm.ptr)
} combiner {
^bb0(%lhs: !llvm.ptr, %rhs: !llvm.ptr):
  %lhs.base = llvm.getelementptr %lhs[0, 0] : (!llvm.ptr) -> !llvm.ptr, !llvm.array<4 x i32>
  %lhs.end = llvm.getelementptr %lhs[0, 4] : (!llvm.ptr) -> !llvm.ptr, !llvm.array<4 x i32>
  %rhs.base = llvm.getelementptr %rhs[0, 0] : (!llvm.ptr) -> !llvm.ptr, !llvm.array<4 x i32>
  llvm.br ^bb1(%lhs.base, %rhs.base : !llvm.ptr, !llvm.ptr)
^bb1(%lhs.it: !llvm.ptr, %rhs.it: !llvm.ptr):  // 2 preds: ^bb0, ^bb2
  %done = llvm.icmp "eq" %lhs.it, %lhs.end : !llvm.ptr
  llvm.cond_br %done, ^bb3, ^bb2
^bb2:
  %lhs.val = llvm.load %lhs.it : !llvm.ptr -> i32
  %rhs.val = llvm.load %rhs.it : !llvm.ptr -> i32
  %sum = llvm.add %lhs.val, %rhs.val : i32
  llvm.store %sum, %lhs.it : i32, !llvm.ptr
  %lhs.next = llvm.getelementptr %lhs.it[1] : (!llvm.ptr) -> !llvm.ptr, i32
  %rhs.next = llvm.getelementptr %rhs.it[1] : (!llvm.ptr) -> !llvm.ptr, i32
  llvm.br ^bb1(%lhs.next, %rhs.next : !llvm.ptr, !llvm.ptr)
^bb3:
  omp.yield(%lhs : !llvm.ptr)
}

llvm.func @parallel_task_byref(%arg0: !llvm.ptr) {
  omp.parallel reduction(mod: task, byref @add_reduction_i_32 %arg0 -> %arg1 : !llvm.ptr) {
    omp.terminator
  }
  llvm.return
}

// CHECK-LABEL: define internal void @parallel_task_byref..omp_par
// CHECK: %[[PRIV:.+]] = alloca [4 x i32], i64 1, align 4
// CHECK: %[[PRIV_SLOT:.+]] = alloca ptr, align 8
// CHECK: %.rd_input. = alloca [1 x %struct.kmp_taskred_input_t], align 8
// CHECK: store ptr %[[PRIV]], ptr %[[SHARED_FIELD:.+]], align 8
// CHECK: store ptr %[[ARG:.+]], ptr %[[ORIG_FIELD:.+]], align 8
// CHECK: store i64 16, ptr %[[SIZE_FIELD:.+]], align 4
// CHECK: store ptr @.omp.task.reduction.init, ptr %[[INIT_FIELD:.+]], align 8
// CHECK: store ptr @.omp.task.reduction.comb, ptr %[[COMB_FIELD:.+]], align 8
// CHECK: call ptr @__kmpc_taskred_modifier_init(ptr @{{.*}}, i32 %{{.*}}, i32 0, i32 1, ptr %.rd_input.)
// CHECK: call void @__kmpc_task_reduction_modifier_fini(ptr @{{.*}}, i32 %{{.*}}, i32 0)
// CHECK: %[[RED_ELEM:.+]] = getelementptr inbounds [1 x ptr], ptr %[[RED_LIST:.+]], i64 0, i64 0
// CHECK: store ptr %[[PRIV_SLOT]], ptr %[[RED_ELEM]], align 8

// CHECK-LABEL: define internal void @.omp.task.reduction.init(ptr %0, ptr %1)
// CHECK: store i32 0, ptr %{{.+}}, align 4
// CHECK: getelementptr [4 x i32], ptr %0, i32 0, i32 0
// CHECK-NOT: store ptr

// CHECK-LABEL: define internal void @.omp.task.reduction.comb(ptr %0, ptr %1)
// CHECK: getelementptr [4 x i32], ptr %0, i32 0, i32 0
// CHECK: getelementptr [4 x i32], ptr %1, i32 0, i32 0
// CHECK: load i32, ptr %{{.+}}, align 4
// CHECK: load i32, ptr %{{.+}}, align 4
// CHECK-NOT: load ptr, ptr %0
