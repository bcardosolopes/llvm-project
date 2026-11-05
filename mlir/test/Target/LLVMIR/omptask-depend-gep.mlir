// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

llvm.func @omp_task_gep_depend() {
  %0 = llvm.mlir.constant(1 : i64) : i64
  %1 = llvm.mlir.constant(0 : i64) : i64
  %2 = llvm.alloca %0 x i32 {bindc_name = "dep"} : (i64) -> !llvm.ptr
  %3 = llvm.getelementptr %2[%1] : (!llvm.ptr, i64) -> !llvm.ptr, i32
  omp.task depend(taskdependin -> %3 : !llvm.ptr) {
    omp.terminator
  }
  llvm.return
}

// CHECK-LABEL: define void @omp_task_gep_depend()
// CHECK: %[[DEP_ARRAY:.+]] = alloca [1 x %struct.kmp_dep_info], align 8
// CHECK: %[[DEP_INFO:.+]] = getelementptr inbounds [1 x %struct.kmp_dep_info], ptr %[[DEP_ARRAY]], i64 0, i64 0
// CHECK: %[[PTR1:.+]] = getelementptr inbounds nuw %struct.kmp_dep_info, ptr %[[DEP_INFO]], i32 0, i32 1
// CHECK: store i64 4, ptr %[[PTR1]], align 4
// CHECK: call i32 @__kmpc_omp_task_with_deps(ptr @{{.+}}, i32 %{{.*}}, ptr %{{.*}}, i32 1, ptr %[[DEP_ARRAY]], i32 0, ptr null)
