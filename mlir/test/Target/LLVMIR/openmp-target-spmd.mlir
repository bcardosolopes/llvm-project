// RUN: split-file %s %t
// RUN: mlir-translate -mlir-to-llvmir %t/host.mlir | FileCheck %s --check-prefix=HOST
// RUN: mlir-translate -mlir-to-llvmir %t/device.mlir | FileCheck %s --check-prefix=DEVICE

//--- host.mlir

module attributes {omp.is_target_device = false, omp.target_triples = ["amdgcn-amd-amdhsa"]} {
  llvm.func @main(%x : i32) {
    omp.target host_eval(%x -> %lb, %x -> %ub, %x -> %step : i32, i32, i32) {
      omp.teams {
        omp.parallel {
          omp.distribute {
            omp.wsloop {
              omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
                omp.yield
              }
            } {omp.composite}
          } {omp.composite}
          omp.terminator
        } {omp.composite}
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }
}





//--- device.mlir

module attributes {dlti.dl_spec = #dlti.dl_spec<#dlti.dl_entry<"dlti.alloca_memory_space", 5 : ui32>>, llvm.target_triple = "amdgcn-amd-amdhsa", omp.is_target_device = true, omp.is_gpu = true} {
  llvm.func @main(%x : i32) {
    omp.target host_eval(%x -> %lb, %x -> %ub, %x -> %step : i32, i32, i32) {
      omp.teams {
        omp.parallel {
          omp.distribute {
            omp.wsloop {
              omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
                omp.yield
              }
            } {omp.composite}
          } {omp.composite}
          omp.terminator
        } {omp.composite}
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }
}

// HOST: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// HOST: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// HOST: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// HOST: @.__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}.region_id = weak constant i8 0
// HOST: @.offloading.entry_name = internal unnamed_addr constant [{{.*}} x i8] c"__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}\00", section ".llvm.rodata.offloading", align 1
// HOST: @.offloading.entry.__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}} = weak constant %struct.__tgt_offload_entry {{[{]}} i64 0, i16 1, i16 1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}.region_id, ptr @.offloading.entry_name, i64 0, i64 0, ptr null }, section "llvm_offload_entries", align 8
// HOST-LABEL: define void @main(
// HOST:   %[[VAL_0:.*]] = alloca %[[VAL_1:.*]], align 8
// HOST:   br label %[[VAL_2:.*]]
// HOST: entry:
// HOST:   %[[VAL_3:.*]] = sub nuw i32 %[[VAL_4:.*]], %[[VAL_4]]
// HOST:   %[[VAL_5:.*]] = icmp ule i32 %[[VAL_4]], %[[VAL_4]]
// HOST:   %[[VAL_6:.*]] = sub i32 %[[VAL_3]], 1
// HOST:   %[[VAL_7:.*]] = udiv i32 %[[VAL_6]], %[[VAL_4]]
// HOST:   %[[VAL_8:.*]] = add i32 %[[VAL_7]], 1
// HOST:   %[[VAL_9:.*]] = icmp ule i32 %[[VAL_3]], %[[VAL_4]]
// HOST:   %[[VAL_10:.*]] = select i1 %[[VAL_9]], i32 1, i32 %[[VAL_8]]
// HOST:   %[[VAL_11:.*]] = select i1 %[[VAL_5]], i32 0, i32 %[[VAL_10]]
// HOST:   %[[VAL_12:.*]] = zext i32 %[[VAL_11]] to i64
// HOST:   %[[VAL_13:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 0
// HOST:   store i32 3, ptr %[[VAL_13]], align 4
// HOST:   %[[VAL_14:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 1
// HOST:   store i32 0, ptr %[[VAL_14]], align 4
// HOST:   %[[VAL_15:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 2
// HOST:   store ptr null, ptr %[[VAL_15]], align 8
// HOST:   %[[VAL_16:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 3
// HOST:   store ptr null, ptr %[[VAL_16]], align 8
// HOST:   %[[VAL_17:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 4
// HOST:   store ptr null, ptr %[[VAL_17]], align 8
// HOST:   %[[VAL_18:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 5
// HOST:   store ptr null, ptr %[[VAL_18]], align 8
// HOST:   %[[VAL_19:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 6
// HOST:   store ptr null, ptr %[[VAL_19]], align 8
// HOST:   %[[VAL_20:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 7
// HOST:   store ptr null, ptr %[[VAL_20]], align 8
// HOST:   %[[VAL_21:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 8
// HOST:   store i64 %[[VAL_12]], ptr %[[VAL_21]], align 8
// HOST:   %[[VAL_22:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 9
// HOST:   store i64 0, ptr %[[VAL_22]], align 8
// HOST:   %[[VAL_23:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 10
// HOST:   store [3 x i32] zeroinitializer, ptr %[[VAL_23]], align 4
// HOST:   %[[VAL_24:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 11
// HOST:   store [3 x i32] zeroinitializer, ptr %[[VAL_24]], align 4
// HOST:   %[[VAL_25:.*]] = getelementptr inbounds nuw %[[VAL_1]], ptr %[[VAL_0]], i32 0, i32 12
// HOST:   store i32 0, ptr %[[VAL_25]], align 4
// HOST:   %[[VAL_26:.*]] = call i32 @__tgt_target_kernel(ptr @1, i64 -1, i32 0, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}.region_id, ptr %[[VAL_0]])
// HOST:   %[[VAL_27:.*]] = icmp ne i32 %[[VAL_26]], 0
// HOST:   br i1 %[[VAL_27]], label %[[VAL_28:.*]], label %[[VAL_29:.*]]
// HOST: omp_offload.failed:
// HOST:   call void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}(i32 %[[VAL_4]], i32 %[[VAL_4]], i32 %[[VAL_4]])
// HOST:   br label %[[VAL_29]]
// HOST: omp_offload.cont:
// HOST:   ret void
// HOST: }
// HOST-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}(
// HOST: entry:
// HOST:   br label %[[VAL_0:.*]]
// HOST: omp.region.after_alloca:
// HOST:   br label %[[VAL_1:.*]]
// HOST: outlined.body:
// HOST:   br label %[[VAL_2:.*]]
// HOST: omp.target:
// HOST:   br label %[[VAL_3:.*]]
// HOST: codeRepl:
// HOST:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.1, i32 %[[VAL_4:.*]])
// HOST:   br label %[[VAL_5:.*]]
// HOST: teams.exit:
// HOST:   br label %[[VAL_6:.*]]
// HOST: omp.region.cont:
// HOST:   ret void
// HOST: }
// HOST-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.1(
// HOST: teams.alloca:
// HOST:   br label %[[VAL_0:.*]]
// HOST: omp.region.after_alloca1:
// HOST:   br label %[[VAL_1:.*]]
// HOST: teams.body:
// HOST:   br label %[[VAL_2:.*]]
// HOST: omp.teams.region:
// HOST:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// HOST:   br label %[[VAL_4:.*]]
// HOST: omp_parallel:
// HOST:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par, i32 %[[VAL_5:.*]])
// HOST:   br label %[[VAL_6:.*]]
// HOST: omp.par.exit:
// HOST:   br label %[[VAL_7:.*]]
// HOST: omp.region.cont2:
// HOST:   br label %[[VAL_8:.*]]
// HOST: teams.exit.exitStub:
// HOST:   ret void
// HOST: }
// HOST-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par(
// HOST: omp.par.entry:
// HOST:   %[[VAL_0:.*]] = alloca i32, align 4
// HOST:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// HOST:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// HOST:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// HOST:   %[[VAL_4:.*]] = alloca i32, align 4
// HOST:   %[[VAL_5:.*]] = alloca i32, align 4
// HOST:   %[[VAL_6:.*]] = alloca i32, align 4
// HOST:   %[[VAL_7:.*]] = alloca i32, align 4
// HOST:   br label %[[VAL_8:.*]]
// HOST: omp.region.after_alloca3:
// HOST:   br label %[[VAL_9:.*]]
// HOST: omp.par.region:
// HOST:   br label %[[VAL_10:.*]]
// HOST: omp.par.region5:
// HOST:   br label %[[VAL_11:.*]]
// HOST: distribute.alloca:
// HOST:   br label %[[VAL_12:.*]]
// HOST: omp.region.after_alloca8:
// HOST:   br label %[[VAL_13:.*]]
// HOST: omp.region.after_alloca6:
// HOST:   br label %[[VAL_14:.*]]
// HOST: distribute.body:
// HOST:   br label %[[VAL_15:.*]]
// HOST: omp.distribute.region:
// HOST:   br label %[[VAL_16:.*]]
// HOST: omp.wsloop.region:
// HOST:   %[[VAL_17:.*]] = icmp slt i32 %[[VAL_18:.*]], 0
// HOST:   %[[VAL_19:.*]] = sub i32 0, %[[VAL_18]]
// HOST:   %[[VAL_20:.*]] = select i1 %[[VAL_17]], i32 %[[VAL_19]], i32 %[[VAL_18]]
// HOST:   %[[VAL_21:.*]] = select i1 %[[VAL_17]], i32 %[[VAL_18]], i32 %[[VAL_18]]
// HOST:   %[[VAL_22:.*]] = select i1 %[[VAL_17]], i32 %[[VAL_18]], i32 %[[VAL_18]]
// HOST:   %[[VAL_23:.*]] = sub nsw i32 %[[VAL_22]], %[[VAL_21]]
// HOST:   %[[VAL_24:.*]] = icmp sle i32 %[[VAL_22]], %[[VAL_21]]
// HOST:   %[[VAL_25:.*]] = sub i32 %[[VAL_23]], 1
// HOST:   %[[VAL_26:.*]] = udiv i32 %[[VAL_25]], %[[VAL_20]]
// HOST:   %[[VAL_27:.*]] = add i32 %[[VAL_26]], 1
// HOST:   %[[VAL_28:.*]] = icmp ule i32 %[[VAL_23]], %[[VAL_20]]
// HOST:   %[[VAL_29:.*]] = select i1 %[[VAL_28]], i32 1, i32 %[[VAL_27]]
// HOST:   %[[VAL_30:.*]] = select i1 %[[VAL_24]], i32 0, i32 %[[VAL_29]]
// HOST:   br label %[[VAL_31:.*]]
// HOST: omp_loop.preheader:
// HOST:   store i32 0, ptr %[[VAL_5]], align 4
// HOST:   %[[VAL_32:.*]] = sub i32 %[[VAL_30]], 1
// HOST:   store i32 %[[VAL_32]], ptr %[[VAL_6]], align 4
// HOST:   store i32 1, ptr %[[VAL_7]], align 4
// HOST:   %[[VAL_33:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// HOST:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_33]], i32 34, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// HOST:   %[[VAL_34:.*]] = load i32, ptr %[[VAL_5]], align 4
// HOST:   %[[VAL_35:.*]] = load i32, ptr %[[VAL_6]], align 4
// HOST:   %[[VAL_36:.*]] = sub i32 %[[VAL_35]], %[[VAL_34]]
// HOST:   %[[VAL_37:.*]] = add i32 %[[VAL_36]], 1
// HOST:   br label %[[VAL_38:.*]]
// HOST: omp_loop.header:
// HOST:   %[[VAL_39:.*]] = phi i32 [ 0, %[[VAL_31]] ], [ %[[VAL_40:.*]], %[[VAL_41:.*]] ]
// HOST:   br label %[[VAL_42:.*]]
// HOST: omp_loop.cond:
// HOST:   %[[VAL_43:.*]] = icmp ult i32 %[[VAL_39]], %[[VAL_37]]
// HOST:   br i1 %[[VAL_43]], label %[[VAL_44:.*]], label %[[VAL_45:.*]]
// HOST: omp_loop.exit:
// HOST:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_33]])
// HOST:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_33]])
// HOST:   br label %[[VAL_46:.*]]
// HOST: omp_loop.after:
// HOST:   br label %[[VAL_47:.*]]
// HOST: omp.region.cont9:
// HOST:   br label %[[VAL_48:.*]]
// HOST: omp.region.cont7:
// HOST:   br label %[[VAL_49:.*]]
// HOST: distribute.exit:
// HOST:   br label %[[VAL_50:.*]]
// HOST: omp.region.cont4:
// HOST:   br label %[[VAL_51:.*]]
// HOST: omp.par.pre_finalize:
// HOST:   br label %[[VAL_52:.*]]
// HOST: .fini:
// HOST:   br label %[[VAL_53:.*]]
// HOST: omp_loop.body:
// HOST:   %[[VAL_54:.*]] = add i32 %[[VAL_39]], %[[VAL_34]]
// HOST:   %[[VAL_55:.*]] = mul i32 %[[VAL_54]], %[[VAL_18]]
// HOST:   %[[VAL_56:.*]] = add i32 %[[VAL_55]], %[[VAL_18]]
// HOST:   br label %[[VAL_57:.*]]
// HOST: omp.loop_nest.region:
// HOST:   br label %[[VAL_58:.*]]
// HOST: omp.region.cont10:
// HOST:   br label %[[VAL_41]]
// HOST: omp_loop.inc:
// HOST:   %[[VAL_40]] = add nuw i32 %[[VAL_39]], 1
// HOST:   br label %[[VAL_38]]
// HOST: omp.par.exit.exitStub:
// HOST:   ret void
// HOST: }

// DEVICE: target datalayout = "A5"
// DEVICE: target triple = "amdgcn-amd-amdhsa"
// DEVICE: @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}_exec_mode = weak protected constant i8 2
// DEVICE: @llvm.compiler.used = appending global [1 x ptr] [ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}_exec_mode], section "llvm.metadata"
// DEVICE: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// DEVICE: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// DEVICE: @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}_dynamic_environment = weak_odr protected global %struct.DynamicEnvironmentTy zeroinitializer
// DEVICE: @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}_kernel_environment = weak_odr protected constant %struct.KernelEnvironmentTy {{[{]}} %struct.ConfigurationEnvironmentTy {{[{]}} i8 0, i8 1, i8 2, i32 1, i32 256, i32 0, i32 0, i32 0, i32 0 }, ptr @1, ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}_dynamic_environment }
// DEVICE-LABEL: define void @main(
// DEVICE:   br label %[[VAL_0:.*]]
// DEVICE: entry:
// DEVICE:   ret void
// DEVICE: }
// DEVICE: define weak_odr protected amdgpu_kernel void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}(ptr %0, i64 %1, i64 %2, i64 %3) #0 {{[{]}}
// DEVICE: entry:
// DEVICE:   %structArg = alloca {{[{]}} ptr addrspace(5) }, align 8, addrspace(5)
// DEVICE:   %structArg.ascast = addrspacecast ptr addrspace(5) %structArg to ptr
// DEVICE:   %4 = alloca i64, align 8, addrspace(5)
// DEVICE:   store i64 %1, ptr addrspace(5) %4, align 4
// DEVICE:   %5 = alloca i64, align 8, addrspace(5)
// DEVICE:   store i64 %2, ptr addrspace(5) %5, align 4
// DEVICE:   %6 = alloca i64, align 8, addrspace(5)
// DEVICE:   store i64 %3, ptr addrspace(5) %6, align 4
// DEVICE:   %gid.addr = alloca i32, align 4, addrspace(5)
// DEVICE:   %tid.addr11 = alloca i32, align 4, addrspace(5)
// DEVICE:   %7 = call i32 @__kmpc_target_init(ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}_kernel_environment, ptr %0)
// DEVICE:   %exec_user_code = icmp eq i32 %7, -1
// DEVICE:   br i1 %exec_user_code, label %user_code.entry, label %worker.exit
// DEVICE: user_code.entry:
// DEVICE:   %8 = load ptr addrspace(5), ptr addrspace(5) %4, align 8
// DEVICE:   %9 = load ptr addrspace(5), ptr addrspace(5) %5, align 8
// DEVICE:   %10 = load ptr addrspace(5), ptr addrspace(5) %6, align 8
// DEVICE:   br label %omp.region.after_alloca
// DEVICE: omp.region.after_alloca:
// DEVICE:   br label %outlined.body
// DEVICE: outlined.body:
// DEVICE:   br label %omp.target
// DEVICE: omp.target:
// DEVICE:   br label %codeRepl
// DEVICE: codeRepl:
// DEVICE:   %gep_ = getelementptr {{[{]}} ptr addrspace(5) }, ptr addrspace(5) %structArg, i32 0, i32 0
// DEVICE:   store ptr addrspace(5) %8, ptr addrspace(5) %gep_, align 8
// DEVICE:   call void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.3(ptr addrspace(5) %gid.addr, ptr addrspace(5) %tid.addr11, ptr %structArg.ascast)
// DEVICE:   br label %teams.exit
// DEVICE: teams.exit:
// DEVICE:   br label %omp.region.cont
// DEVICE: omp.region.cont:
// DEVICE:   call void @__kmpc_target_deinit()
// DEVICE:   ret void
// DEVICE: worker.exit:
// DEVICE:   ret void
// DEVICE: }
// DEVICE-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.3(
// DEVICE: teams.alloca:
// DEVICE:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr addrspace(5) }, ptr %[[VAL_1:.*]], i32 0, i32 0
// DEVICE:   %[[VAL_2:.*]] = load ptr addrspace(5), ptr %[[VAL_0]], align 8, !align !1
// DEVICE:   %[[VAL_3:.*]] = alloca [1 x ptr], align 8, addrspace(5)
// DEVICE:   %[[VAL_4:.*]] = addrspacecast ptr addrspace(5) %[[VAL_3]] to ptr
// DEVICE:   %[[VAL_5:.*]] = alloca {{[{]}} ptr addrspace(5) }, align 8, addrspace(5)
// DEVICE:   %[[VAL_6:.*]] = addrspacecast ptr addrspace(5) %[[VAL_5]] to ptr
// DEVICE:   %[[VAL_7:.*]] = alloca i32, align 4, addrspace(5)
// DEVICE:   br label %[[VAL_8:.*]]
// DEVICE: omp.region.after_alloca1:
// DEVICE:   %[[VAL_9:.*]] = load i32, ptr addrspace(5) %[[VAL_10:.*]], align 4
// DEVICE:   %[[VAL_11:.*]] = load i32, ptr addrspace(5) %[[VAL_12:.*]], align 4
// DEVICE:   br label %[[VAL_13:.*]]
// DEVICE: teams.body:
// DEVICE:   br label %[[VAL_14:.*]]
// DEVICE: omp.teams.region:
// DEVICE:   %[[VAL_15:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// DEVICE:   store ptr addrspace(5) %[[VAL_2]], ptr addrspace(5) %[[VAL_7]], align 4
// DEVICE:   br label %[[VAL_16:.*]]
// DEVICE: omp_parallel:
// DEVICE:   %[[VAL_17:.*]] = getelementptr {{[{]}} ptr addrspace(5) }, ptr addrspace(5) %[[VAL_5]], i32 0, i32 0
// DEVICE:   store ptr addrspace(5) %[[VAL_7]], ptr addrspace(5) %[[VAL_17]], align 8
// DEVICE:   %[[VAL_18:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_4]], i64 0, i64 0
// DEVICE:   store ptr %[[VAL_6]], ptr %[[VAL_18]], align 8
// DEVICE:   call void @__kmpc_parallel_60(ptr @1, i32 %[[VAL_15]], i32 1, i32 -1, i32 -1, ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.2, ptr null, ptr %[[VAL_4]], i64 1, i32 0)
// DEVICE:   br label %[[VAL_19:.*]]
// DEVICE: omp.par.exit:
// DEVICE:   br label %[[VAL_20:.*]]
// DEVICE: omp.region.cont2:
// DEVICE:   br label %[[VAL_21:.*]]
// DEVICE: teams.exit.exitStub:
// DEVICE:   ret void
// DEVICE: }
// DEVICE-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.2(
// DEVICE: omp.par.entry:
// DEVICE:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr addrspace(5) }, ptr %[[VAL_1:.*]], i32 0, i32 0
// DEVICE:   %[[VAL_2:.*]] = load ptr addrspace(5), ptr %[[VAL_0]], align 8, !align !2
// DEVICE:   %[[VAL_3:.*]] = alloca {{[{]}} i32 }, align 8, addrspace(5)
// DEVICE:   %[[VAL_4:.*]] = addrspacecast ptr addrspace(5) %[[VAL_3]] to ptr
// DEVICE:   %[[VAL_5:.*]] = alloca i32, align 4, addrspace(5)
// DEVICE:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_7:.*]], align 4
// DEVICE:   store i32 %[[VAL_6]], ptr addrspace(5) %[[VAL_5]], align 4
// DEVICE:   %[[VAL_8:.*]] = load i32, ptr addrspace(5) %[[VAL_5]], align 4
// DEVICE:   %[[VAL_9:.*]] = load i32, ptr addrspace(5) %[[VAL_2]], align 4
// DEVICE:   br label %[[VAL_10:.*]]
// DEVICE: omp.region.after_alloca3:
// DEVICE:   br label %[[VAL_11:.*]]
// DEVICE: omp.par.region:
// DEVICE:   br label %[[VAL_12:.*]]
// DEVICE: omp.par.region5:
// DEVICE:   br label %[[VAL_13:.*]]
// DEVICE: codeRepl:
// DEVICE:   %[[VAL_14:.*]] = getelementptr {{[{]}} i32 }, ptr addrspace(5) %[[VAL_3]], i32 0, i32 0
// DEVICE:   store i32 %[[VAL_9]], ptr addrspace(5) %[[VAL_14]], align 4
// DEVICE:   call void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.1(ptr %[[VAL_4]])
// DEVICE:   br label %[[VAL_15:.*]]
// DEVICE: distribute.exit:
// DEVICE:   br label %[[VAL_16:.*]]
// DEVICE: omp.region.cont4:
// DEVICE:   br label %[[VAL_17:.*]]
// DEVICE: omp.par.pre_finalize:
// DEVICE:   br label %[[VAL_18:.*]]
// DEVICE: .fini:
// DEVICE:   br label %[[VAL_19:.*]]
// DEVICE: omp.par.exit.exitStub:
// DEVICE:   ret void
// DEVICE: }
// DEVICE-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.1(
// DEVICE: distribute.alloca:
// DEVICE:   %[[VAL_0:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_1:.*]], i32 0, i32 0
// DEVICE:   %[[VAL_2:.*]] = load i32, ptr %[[VAL_0]], align 4
// DEVICE:   %[[VAL_3:.*]] = alloca {{[{]}} i32 }, align 8, addrspace(5)
// DEVICE:   %[[VAL_4:.*]] = addrspacecast ptr addrspace(5) %[[VAL_3]] to ptr
// DEVICE:   br label %[[VAL_5:.*]]
// DEVICE: omp.region.after_alloca8:
// DEVICE:   br label %[[VAL_6:.*]]
// DEVICE: omp.region.after_alloca6:
// DEVICE:   br label %[[VAL_7:.*]]
// DEVICE: distribute.body:
// DEVICE:   br label %[[VAL_8:.*]]
// DEVICE: omp.distribute.region:
// DEVICE:   br label %[[VAL_9:.*]]
// DEVICE: omp.wsloop.region:
// DEVICE:   %[[VAL_10:.*]] = icmp slt i32 %[[VAL_2]], 0
// DEVICE:   %[[VAL_11:.*]] = sub i32 0, %[[VAL_2]]
// DEVICE:   %[[VAL_12:.*]] = select i1 %[[VAL_10]], i32 %[[VAL_11]], i32 %[[VAL_2]]
// DEVICE:   %[[VAL_13:.*]] = select i1 %[[VAL_10]], i32 %[[VAL_2]], i32 %[[VAL_2]]
// DEVICE:   %[[VAL_14:.*]] = select i1 %[[VAL_10]], i32 %[[VAL_2]], i32 %[[VAL_2]]
// DEVICE:   %[[VAL_15:.*]] = sub nsw i32 %[[VAL_14]], %[[VAL_13]]
// DEVICE:   %[[VAL_16:.*]] = icmp sle i32 %[[VAL_14]], %[[VAL_13]]
// DEVICE:   %[[VAL_17:.*]] = sub i32 %[[VAL_15]], 1
// DEVICE:   %[[VAL_18:.*]] = udiv i32 %[[VAL_17]], %[[VAL_12]]
// DEVICE:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// DEVICE:   %[[VAL_20:.*]] = icmp ule i32 %[[VAL_15]], %[[VAL_12]]
// DEVICE:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i32 1, i32 %[[VAL_19]]
// DEVICE:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i32 0, i32 %[[VAL_21]]
// DEVICE:   br label %[[VAL_23:.*]]
// DEVICE: omp_loop.preheader:
// DEVICE:   %[[VAL_24:.*]] = getelementptr {{[{]}} i32 }, ptr addrspace(5) %[[VAL_3]], i32 0, i32 0
// DEVICE:   store i32 %[[VAL_2]], ptr addrspace(5) %[[VAL_24]], align 4
// DEVICE:   %[[VAL_25:.*]] = call i32 @omp_get_num_threads()
// DEVICE:   call void @__kmpc_distribute_for_static_loop_4u(ptr @1, ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par, ptr %[[VAL_4]], i32 %[[VAL_22]], i32 %[[VAL_25]], i32 0, i32 0, i8 0)
// DEVICE:   br label %[[VAL_26:.*]]
// DEVICE: omp_loop.exit:
// DEVICE:   br label %[[VAL_27:.*]]
// DEVICE: omp_loop.after:
// DEVICE:   br label %[[VAL_28:.*]]
// DEVICE: omp.region.cont9:
// DEVICE:   br label %[[VAL_29:.*]]
// DEVICE: omp.region.cont7:
// DEVICE:   br label %[[VAL_30:.*]]
// DEVICE: distribute.exit.exitStub:
// DEVICE:   ret void
// DEVICE: }
// DEVICE-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par(
// DEVICE: omp_loop.body:
// DEVICE:   %[[VAL_0:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_1:.*]], i32 0, i32 0
// DEVICE:   %[[VAL_2:.*]] = load i32, ptr %[[VAL_0]], align 4
// DEVICE:   %[[VAL_3:.*]] = mul i32 %[[VAL_4:.*]], %[[VAL_2]]
// DEVICE:   %[[VAL_5:.*]] = add i32 %[[VAL_3]], %[[VAL_2]]
// DEVICE:   br label %[[VAL_6:.*]]
// DEVICE: omp.loop_nest.region:
// DEVICE:   br label %[[VAL_7:.*]]
// DEVICE: omp.region.cont10:
// DEVICE:   br label %[[VAL_8:.*]]
// DEVICE: omp.prelatch.exitStub:
// DEVICE:   ret void
// DEVICE: }
