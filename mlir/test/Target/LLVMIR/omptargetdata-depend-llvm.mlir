// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

module attributes {omp.target_triples = ["amdgcn-amd-amdhsa"]} {
  llvm.func @target_enter_data_depend_local() {
    %0 = llvm.mlir.constant(1 : i64) : i64
    %dep = llvm.alloca %0 x i32 {bindc_name = "dep", in_type = i32, operand_segment_sizes = array<i32: 0, 0>, uniq_name = "dep"} : (i64) -> !llvm.ptr
    %x = llvm.alloca %0 x i32 {bindc_name = "x", in_type = i32, operand_segment_sizes = array<i32: 0, 0>, uniq_name = "x"} : (i64) -> !llvm.ptr
    %map = omp.map.info var_ptr(%x : !llvm.ptr, i32) map_clauses(to) capture(ByRef) -> !llvm.ptr {name = "x"}
    omp.target_enter_data depend(taskdependin -> %dep : !llvm.ptr) map_entries(%map : !llvm.ptr)
    llvm.return
  }

  llvm.func @target_update_depend_local() {
    %0 = llvm.mlir.constant(1 : i64) : i64
    %dep = llvm.alloca %0 x i32 {bindc_name = "dep", in_type = i32, operand_segment_sizes = array<i32: 0, 0>, uniq_name = "dep"} : (i64) -> !llvm.ptr
    %x = llvm.alloca %0 x i32 {bindc_name = "x", in_type = i32, operand_segment_sizes = array<i32: 0, 0>, uniq_name = "x"} : (i64) -> !llvm.ptr
    %map = omp.map.info var_ptr(%x : !llvm.ptr, i32) map_clauses(to) capture(ByRef) -> !llvm.ptr {name = "x"}
    omp.target_update depend(taskdependin -> %dep : !llvm.ptr) map_entries(%map : !llvm.ptr)
    llvm.return
  }

  llvm.func @target_exit_data_depend_local() {
    %0 = llvm.mlir.constant(1 : i64) : i64
    %dep = llvm.alloca %0 x i32 {bindc_name = "dep", in_type = i32, operand_segment_sizes = array<i32: 0, 0>, uniq_name = "dep"} : (i64) -> !llvm.ptr
    %x = llvm.alloca %0 x i32 {bindc_name = "x", in_type = i32, operand_segment_sizes = array<i32: 0, 0>, uniq_name = "x"} : (i64) -> !llvm.ptr
    %map = omp.map.info var_ptr(%x : !llvm.ptr, i32) map_clauses(from) capture(ByRef) -> !llvm.ptr {name = "x"}
    omp.target_exit_data depend(taskdependin -> %dep : !llvm.ptr) map_entries(%map : !llvm.ptr)
    llvm.return
  }

  llvm.func @target_update_nowait_depend_local(%len: i32) {
    %0 = llvm.mlir.constant(1 : i64) : i64
    %dep = llvm.alloca %0 x i32 {bindc_name = "dep", in_type = i32, operand_segment_sizes = array<i32: 0, 0>, uniq_name = "dep"} : (i64) -> !llvm.ptr
    %x = llvm.alloca %0 x i32 {bindc_name = "x", in_type = i32, operand_segment_sizes = array<i32: 0, 0>, uniq_name = "x"} : (i64) -> !llvm.ptr
    %map = omp.map.info var_ptr(%x : !llvm.ptr, i32) map_clauses(to) capture(ByRef) -> !llvm.ptr {name = "x"}
    omp.target_update depend(taskdependinout -> %dep : !llvm.ptr size(%len : i32)) nowait map_entries(%map : !llvm.ptr) {cir.omp.depend_sizes_owned}
    llvm.return
  }
}

// CHECK-LABEL: define void @target_enter_data_depend_local()
// CHECK: %[[ENTER_DEP_ARRAY:.+]] = alloca [1 x %struct.kmp_dep_info], align 8
// CHECK: %[[ENTER_TASK:.+]] = call ptr @__kmpc_omp_task_alloc(
// CHECK: %[[ENTER_DEP_INFO:.+]] = getelementptr inbounds [1 x %struct.kmp_dep_info], ptr %[[ENTER_DEP_ARRAY]], i64 0, i64 0
// CHECK: %[[ENTER_DEP_LEN:.+]] = getelementptr inbounds nuw %struct.kmp_dep_info, ptr %[[ENTER_DEP_INFO]], i32 0, i32 1
// CHECK: store i64 4, ptr %[[ENTER_DEP_LEN]], align 4
// CHECK: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.*}}, i32 1, ptr %[[ENTER_DEP_ARRAY]], i32 0, ptr null, i32 0)
// CHECK: call void @__kmpc_omp_task_begin_if0(ptr @{{.+}}, i32 %{{.*}}, ptr %[[ENTER_TASK]])
// CHECK: call void @__kmpc_omp_task_complete_if0(ptr @{{.+}}, i32 %{{.*}}, ptr %[[ENTER_TASK]])
// CHECK: call void @__tgt_target_data_begin_mapper(

// CHECK-LABEL: define void @target_update_depend_local()
// CHECK: %[[UPDATE_DEP_ARRAY:.+]] = alloca [1 x %struct.kmp_dep_info], align 8
// CHECK: %[[UPDATE_TASK:.+]] = call ptr @__kmpc_omp_task_alloc(
// CHECK: %[[UPDATE_DEP_INFO:.+]] = getelementptr inbounds [1 x %struct.kmp_dep_info], ptr %[[UPDATE_DEP_ARRAY]], i64 0, i64 0
// CHECK: %[[UPDATE_DEP_LEN:.+]] = getelementptr inbounds nuw %struct.kmp_dep_info, ptr %[[UPDATE_DEP_INFO]], i32 0, i32 1
// CHECK: store i64 4, ptr %[[UPDATE_DEP_LEN]], align 4
// CHECK: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.*}}, i32 1, ptr %[[UPDATE_DEP_ARRAY]], i32 0, ptr null, i32 0)
// CHECK: call void @__kmpc_omp_task_begin_if0(ptr @{{.+}}, i32 %{{.*}}, ptr %[[UPDATE_TASK]])
// CHECK: call void @__kmpc_omp_task_complete_if0(ptr @{{.+}}, i32 %{{.*}}, ptr %[[UPDATE_TASK]])
// CHECK: call void @__tgt_target_data_update_mapper(

// CHECK-LABEL: define void @target_exit_data_depend_local()
// CHECK: %[[EXIT_DEP_ARRAY:.+]] = alloca [1 x %struct.kmp_dep_info], align 8
// CHECK: %[[EXIT_TASK:.+]] = call ptr @__kmpc_omp_task_alloc(
// CHECK: %[[EXIT_DEP_INFO:.+]] = getelementptr inbounds [1 x %struct.kmp_dep_info], ptr %[[EXIT_DEP_ARRAY]], i64 0, i64 0
// CHECK: %[[EXIT_DEP_LEN:.+]] = getelementptr inbounds nuw %struct.kmp_dep_info, ptr %[[EXIT_DEP_INFO]], i32 0, i32 1
// CHECK: store i64 4, ptr %[[EXIT_DEP_LEN]], align 4
// CHECK: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.*}}, i32 1, ptr %[[EXIT_DEP_ARRAY]], i32 0, ptr null, i32 0)
// CHECK: call void @__kmpc_omp_task_begin_if0(ptr @{{.+}}, i32 %{{.*}}, ptr %[[EXIT_TASK]])
// CHECK: call void @__kmpc_omp_task_complete_if0(ptr @{{.+}}, i32 %{{.*}}, ptr %[[EXIT_TASK]])
// CHECK: call void @__tgt_target_data_end_mapper(

// CHECK-LABEL: define void @target_update_nowait_depend_local(i32 %{{.+}})
// CHECK: %[[NOWAIT_DEP_ARRAY:.+]] = alloca [1 x %struct.kmp_dep_info], align 8
// CHECK: call ptr @__kmpc_omp_target_task_alloc(
// CHECK: %[[NOWAIT_DEP_INFO:.+]] = getelementptr inbounds [1 x %struct.kmp_dep_info], ptr %[[NOWAIT_DEP_ARRAY]], i64 0, i64 0
// CHECK: %[[NOWAIT_DEP_LEN:.+]] = getelementptr inbounds nuw %struct.kmp_dep_info, ptr %[[NOWAIT_DEP_INFO]], i32 0, i32 1
// CHECK: store i64 %{{.+}}, ptr %[[NOWAIT_DEP_LEN]], align 4
// CHECK: call i32 @__kmpc_omp_task_with_deps(ptr @{{.+}}, i32 %{{.*}}, ptr %{{.*}}, i32 1, ptr %[[NOWAIT_DEP_ARRAY]], i32 0, ptr null)
// CHECK: call void @__tgt_target_data_update_nowait_mapper(
