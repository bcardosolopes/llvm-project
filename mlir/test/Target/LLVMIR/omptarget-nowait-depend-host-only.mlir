// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

module attributes {omp.is_target_device = false} {
  llvm.func @omp_target_nowait_depend_() {
    %0 = llvm.mlir.constant(1 : i64) : i64
    %1 = llvm.alloca %0 x i32 {bindc_name = "dep"} : (i64) -> !llvm.ptr
    %2 = llvm.mlir.constant(0 : i64) : i64
    %3 = llvm.getelementptr %1[%2] : (!llvm.ptr, i64) -> !llvm.ptr, i32
    %4 = llvm.alloca %0 x f32 {bindc_name = "x"} : (i64) -> !llvm.ptr
    %5 = omp.map.info var_ptr(%4 : !llvm.ptr, f32) map_clauses(tofrom) capture(ByRef) -> !llvm.ptr {name = "x"}
    omp.target depend(taskdependinout -> %3 : !llvm.ptr) nowait map_entries(%5 -> %arg0 : !llvm.ptr) {
      %6 = llvm.mlir.constant(5.000000e+00 : f32) : f32
      llvm.store %6, %arg0 : f32, !llvm.ptr
      omp.terminator
    }
    llvm.return
  }
}

// CHECK-LABEL: define void @omp_target_nowait_depend_()
// CHECK: %[[DEP_ARRAY:.+]] = alloca [1 x %struct.kmp_dep_info], align 8
// CHECK-NOT: call ptr @__kmpc_omp_task_alloc
// CHECK: %[[TASKDATA:.+]] = call ptr @__kmpc_omp_target_task_alloc({{.+}}, i64 -1)
// CHECK: %[[DEP_INFO:.+]] = getelementptr inbounds [1 x %struct.kmp_dep_info], ptr %[[DEP_ARRAY]], i64 0, i64 0
// CHECK: %[[PTR1:.+]] = getelementptr inbounds nuw %struct.kmp_dep_info, ptr %[[DEP_INFO]], i32 0, i32 1
// CHECK: store i64 4, ptr %[[PTR1]], align 4
// CHECK-NOT: call void @__kmpc_omp_taskwait_deps_51
// CHECK-NOT: call void @__kmpc_omp_task_begin_if0
// CHECK-NOT: call void @__kmpc_omp_task_complete_if0
// CHECK: call i32 @__kmpc_omp_task_with_deps(ptr @{{.+}}, i32 %{{.*}}, ptr %[[TASKDATA]], i32 1, ptr %[[DEP_ARRAY]], i32 0, ptr null)
