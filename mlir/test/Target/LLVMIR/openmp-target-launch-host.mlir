// RUN: mlir-translate -split-input-file -mlir-to-llvmir %s | FileCheck %s





module attributes {omp.is_target_device = false, omp.target_triples = ["amdgcn-amd-amdhsa"]} {
  llvm.func @main(%num_teams : i32) {
    %target_threads = llvm.mlir.constant(20) : i32
    %teams_threads = llvm.mlir.constant(10) : i32
    omp.target thread_limit(%target_threads : i32)
               host_eval(%num_teams -> %arg_teams, %teams_threads -> %arg_teams_threads : i32, i32) {
      omp.teams num_teams(to %arg_teams : i32) thread_limit(%arg_teams_threads : i32) {
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }
}

// -----

module attributes {omp.is_target_device = false, omp.target_triples = ["nvptx64-nvidia-cuda"]} {
  llvm.func @parallel_if_num_threads(%cond : i1, %n : i32) {
    omp.target host_eval(%cond -> %arg0, %n -> %arg1 : i1, i32) {
      omp.parallel if(%arg0) num_threads(%arg1 : i32) {
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }
}

// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @.__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}.region_id = weak constant i8 0
// CHECK: @.offloading.entry_name = internal unnamed_addr constant [{{.*}} x i8] c"__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}\00", section ".llvm.rodata.offloading", align 1
// CHECK: @.offloading.entry.__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}} = weak constant %struct.__tgt_offload_entry {{[{]}} i64 0, i16 1, i16 1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}.region_id, ptr @.offloading.entry_name, i64 0, i64 0, ptr null }, section "llvm_offload_entries", align 8
// CHECK-LABEL: define void @main(
// CHECK:   %[[VAL_0:.*]] = alloca %[[VAL_1:.*]], align 8
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_3:.*]] = insertvalue [3 x i32] zeroinitializer, i32 %[[VAL_4:.*]], 0
// CHECK:   %[[VAL_5:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store i32 3, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_6:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   store i32 0, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_7:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   store ptr null, ptr %[[VAL_7]], align 8
// CHECK:   %[[VAL_8:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 3
// CHECK:   store ptr null, ptr %[[VAL_8]], align 8
// CHECK:   %[[VAL_9:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 4
// CHECK:   store ptr null, ptr %[[VAL_9]], align 8
// CHECK:   %[[VAL_10:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 5
// CHECK:   store ptr null, ptr %[[VAL_10]], align 8
// CHECK:   %[[VAL_11:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 6
// CHECK:   store ptr null, ptr %[[VAL_11]], align 8
// CHECK:   %[[VAL_12:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 7
// CHECK:   store ptr null, ptr %[[VAL_12]], align 8
// CHECK:   %[[VAL_13:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 8
// CHECK:   store i64 0, ptr %[[VAL_13]], align 8
// CHECK:   %[[VAL_14:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 9
// CHECK:   store i64 0, ptr %[[VAL_14]], align 8
// CHECK:   %[[VAL_15:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 10
// CHECK:   store [3 x i32] %[[VAL_3]], ptr %[[VAL_15]], align 4
// CHECK:   %[[VAL_16:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 11
// CHECK:   store [3 x i32] [i32 10, i32 0, i32 0], ptr %[[VAL_16]], align 4
// CHECK:   %[[VAL_17:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 12
// CHECK:   store i32 0, ptr %[[VAL_17]], align 4
// CHECK:   %[[VAL_18:.*]] = call i32 @__tgt_target_kernel(ptr @1, i64 -1, i32 %[[VAL_4]], i32 10, ptr @.__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}.region_id, ptr %[[VAL_0]])
// CHECK:   %[[VAL_19:.*]] = icmp ne i32 %[[VAL_18]], 0
// CHECK:   br i1 %[[VAL_19]], label %[[VAL_20:.*]], label %[[VAL_21:.*]]
// CHECK: omp_offload.failed:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}(i32 %[[VAL_4]])
// CHECK:   br label %[[VAL_21]]
// CHECK: omp_offload.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}(
// CHECK: entry:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_push_num_teams_51(ptr @1, i32 %[[VAL_3]], i32 %[[VAL_4:.*]], i32 %[[VAL_4]], i32 10)
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 0, ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par)
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: teams.exit:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par(
// CHECK: teams.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: teams.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.teams.region:
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @.__omp_offloading_{{.*}}_{{.*}}_parallel_if_num_threads_l{{.*}}.region_id = weak constant i8 0
// CHECK: @.offloading.entry_name = internal unnamed_addr constant [{{.*}} x i8] c"__omp_offloading_{{.*}}_{{.*}}_parallel_if_num_threads_l{{.*}}\00", section ".llvm.rodata.offloading", align 1
// CHECK: @.offloading.entry.__omp_offloading_{{.*}}_{{.*}}_parallel_if_num_threads_l{{.*}} = weak constant %struct.__tgt_offload_entry {{[{]}} i64 0, i16 1, i16 1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}_parallel_if_num_threads_l{{.*}}.region_id, ptr @.offloading.entry_name, i64 0, i64 0, ptr null }, section "llvm_offload_entries", align 8
// CHECK-LABEL: define void @parallel_if_num_threads(
// CHECK:   %[[VAL_0:.*]] = alloca %[[VAL_1:.*]], align 8
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_3:.*]] = select i1 %[[VAL_4:.*]], i32 %[[VAL_5:.*]], i32 1
// CHECK:   %[[VAL_6:.*]] = insertvalue [3 x i32] zeroinitializer, i32 %[[VAL_3]], 0
// CHECK:   %[[VAL_7:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store i32 3, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_8:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 1
// CHECK:   store i32 0, ptr %[[VAL_8]], align 4
// CHECK:   %[[VAL_9:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 2
// CHECK:   store ptr null, ptr %[[VAL_9]], align 8
// CHECK:   %[[VAL_10:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 3
// CHECK:   store ptr null, ptr %[[VAL_10]], align 8
// CHECK:   %[[VAL_11:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 4
// CHECK:   store ptr null, ptr %[[VAL_11]], align 8
// CHECK:   %[[VAL_12:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 5
// CHECK:   store ptr null, ptr %[[VAL_12]], align 8
// CHECK:   %[[VAL_13:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 6
// CHECK:   store ptr null, ptr %[[VAL_13]], align 8
// CHECK:   %[[VAL_14:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 7
// CHECK:   store ptr null, ptr %[[VAL_14]], align 8
// CHECK:   %[[VAL_15:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 8
// CHECK:   store i64 0, ptr %[[VAL_15]], align 8
// CHECK:   %[[VAL_16:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 9
// CHECK:   store i64 0, ptr %[[VAL_16]], align 8
// CHECK:   %[[VAL_17:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 10
// CHECK:   store [3 x i32] [i32 1, i32 0, i32 0], ptr %[[VAL_17]], align 4
// CHECK:   %[[VAL_18:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 11
// CHECK:   store [3 x i32] %[[VAL_6]], ptr %[[VAL_18]], align 4
// CHECK:   %[[VAL_19:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 12
// CHECK:   store i32 0, ptr %[[VAL_19]], align 4
// CHECK:   %[[VAL_20:.*]] = call i32 @__tgt_target_kernel(ptr @1, i64 -1, i32 1, i32 %[[VAL_3]], ptr @.__omp_offloading_{{.*}}_{{.*}}_parallel_if_num_threads_l{{.*}}.region_id, ptr %[[VAL_0]])
// CHECK:   %[[VAL_21:.*]] = icmp ne i32 %[[VAL_20]], 0
// CHECK:   br i1 %[[VAL_21]], label %[[VAL_22:.*]], label %[[VAL_23:.*]]
// CHECK: omp_offload.failed:
// CHECK:   call void @__omp_offloading_{{.*}}_{{.*}}_parallel_if_num_threads_l{{.*}}(i1 %[[VAL_4]], i32 %[[VAL_5]])
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_offload.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_parallel_if_num_threads_l{{.*}}(
// CHECK: entry:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: outlined.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.target:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_push_num_threads(ptr @1, i32 %[[VAL_3]], i32 %[[VAL_4:.*]])
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp_parallel:
// CHECK:   %[[VAL_6:.*]] = sext i1 %[[VAL_7:.*]] to i32
// CHECK:   call void @__kmpc_fork_call_if(ptr @1, i32 0, ptr @__omp_offloading_{{.*}}_{{.*}}_parallel_if_num_threads_l{{.*}}..omp_par, i32 %[[VAL_6]], ptr null)
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_parallel_if_num_threads_l{{.*}}..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region3:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
