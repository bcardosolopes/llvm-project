// RUN: split-file %s %t
// RUN: mlir-translate -mlir-to-llvmir %t/host.mlir | FileCheck %s --check-prefix=HOST
// RUN: mlir-translate -mlir-to-llvmir %t/device.mlir | FileCheck %s --check-prefix=DEVICE

//--- host.mlir

module attributes {omp.is_target_device = false, omp.target_triples = ["amdgcn-amd-amdhsa"]} {
  llvm.func @main(%arg0 : !llvm.ptr) {
    %x = llvm.load %arg0 : !llvm.ptr -> i32
    %0 = omp.map.info var_ptr(%arg0 : !llvm.ptr, i32) map_clauses(to) capture(ByCopy) -> !llvm.ptr
    omp.target host_eval(%x -> %lb, %x -> %ub, %x -> %step : i32, i32, i32) map_entries(%0 -> %ptr : !llvm.ptr) {
      %x.map = llvm.load %ptr : !llvm.ptr -> i32
      omp.teams {
        omp.distribute {
          omp.loop_nest (%iv1) : i32 = (%lb) to (%ub) step (%step) {
            omp.parallel {
              omp.wsloop {
                omp.loop_nest (%iv2) : i32 = (%x.map) to (%x.map) step (%x.map) {
                  omp.yield
                }
              }
              omp.terminator
            }
            omp.yield
          }
        }
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }
}





//--- device.mlir

module attributes {dlti.dl_spec = #dlti.dl_spec<#dlti.dl_entry<"dlti.alloca_memory_space", 5 : ui32>>, llvm.target_triple = "amdgcn-amd-amdhsa", omp.is_target_device = true, omp.is_gpu = true} {
  llvm.func @main(%arg0 : !llvm.ptr) {
    %0 = omp.map.info var_ptr(%arg0 : !llvm.ptr, i32) map_clauses(to) capture(ByCopy) -> !llvm.ptr
    omp.target map_entries(%0 -> %ptr : !llvm.ptr) {
      %x = llvm.load %ptr : !llvm.ptr -> i32
      omp.teams {
        omp.distribute {
          omp.loop_nest (%iv1) : i32 = (%x) to (%x) step (%x) {
            omp.parallel {
              omp.wsloop {
                omp.loop_nest (%iv2) : i32 = (%x) to (%x) step (%x) {
                  omp.yield
                }
              }
              omp.terminator
            }
            omp.yield
          }
        }
        omp.terminator
      }
      omp.terminator
    }
    llvm.return
  }
}

// HOST: @0 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}}/host.mlir;unknown;{{.*}};{{.*}};;\00", align 1
// HOST: @1 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// HOST: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @1 }, align 8
// HOST: @3 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @1 }, align 8
// HOST: @.__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}.region_id = weak constant i8 0
// HOST: @.offload_sizes = private unnamed_addr constant [1 x i64] [i64 4]
// HOST: @.offload_maptypes = private unnamed_addr constant [1 x i64] [i64 289]
// HOST: @.offload_mapnames = private constant [1 x ptr] [ptr @0]
// HOST: @.offloading.entry_name = internal unnamed_addr constant [{{.*}} x i8] c"__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}\00", section ".llvm.rodata.offloading", align 1
// HOST: @.offloading.entry.__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}} = weak constant %struct.__tgt_offload_entry {{[{]}} i64 0, i16 1, i16 1, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}.region_id, ptr @.offloading.entry_name, i64 0, i64 0, ptr null }, section "llvm_offload_entries", align 8
// HOST-LABEL: define void @main(
// HOST:   %[[VAL_0:.*]] = alloca i64, align 8
// HOST:   %[[VAL_1:.*]] = alloca [1 x ptr], align 8
// HOST:   %[[VAL_2:.*]] = alloca [1 x ptr], align 8
// HOST:   %[[VAL_3:.*]] = alloca [1 x ptr], align 8
// HOST:   %[[VAL_4:.*]] = alloca %[[VAL_5:.*]], align 8
// HOST:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_7:.*]], align 4
// HOST:   br label %[[VAL_8:.*]]
// HOST: entry:
// HOST:   %[[VAL_9:.*]] = load i32, ptr %[[VAL_7]], align 4
// HOST:   store i32 %[[VAL_9]], ptr %[[VAL_0]], align 4
// HOST:   %[[VAL_10:.*]] = load i64, ptr %[[VAL_0]], align 4
// HOST:   %[[VAL_11:.*]] = sub nuw i32 %[[VAL_6]], %[[VAL_6]]
// HOST:   %[[VAL_12:.*]] = icmp ule i32 %[[VAL_6]], %[[VAL_6]]
// HOST:   %[[VAL_13:.*]] = sub i32 %[[VAL_11]], 1
// HOST:   %[[VAL_14:.*]] = udiv i32 %[[VAL_13]], %[[VAL_6]]
// HOST:   %[[VAL_15:.*]] = add i32 %[[VAL_14]], 1
// HOST:   %[[VAL_16:.*]] = icmp ule i32 %[[VAL_11]], %[[VAL_6]]
// HOST:   %[[VAL_17:.*]] = select i1 %[[VAL_16]], i32 1, i32 %[[VAL_15]]
// HOST:   %[[VAL_18:.*]] = select i1 %[[VAL_12]], i32 0, i32 %[[VAL_17]]
// HOST:   %[[VAL_19:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_1]], i32 0, i32 0
// HOST:   store i64 %[[VAL_10]], ptr %[[VAL_19]], align 8
// HOST:   %[[VAL_20:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_2]], i32 0, i32 0
// HOST:   store i64 %[[VAL_10]], ptr %[[VAL_20]], align 8
// HOST:   %[[VAL_21:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_3]], i64 0, i64 0
// HOST:   store ptr null, ptr %[[VAL_21]], align 8
// HOST:   %[[VAL_22:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_1]], i32 0, i32 0
// HOST:   %[[VAL_23:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_2]], i32 0, i32 0
// HOST:   %[[VAL_24:.*]] = zext i32 %[[VAL_18]] to i64
// HOST:   %[[VAL_25:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 0
// HOST:   store i32 3, ptr %[[VAL_25]], align 4
// HOST:   %[[VAL_26:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 1
// HOST:   store i32 1, ptr %[[VAL_26]], align 4
// HOST:   %[[VAL_27:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 2
// HOST:   store ptr %[[VAL_22]], ptr %[[VAL_27]], align 8
// HOST:   %[[VAL_28:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 3
// HOST:   store ptr %[[VAL_23]], ptr %[[VAL_28]], align 8
// HOST:   %[[VAL_29:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 4
// HOST:   store ptr @.offload_sizes, ptr %[[VAL_29]], align 8
// HOST:   %[[VAL_30:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 5
// HOST:   store ptr @.offload_maptypes, ptr %[[VAL_30]], align 8
// HOST:   %[[VAL_31:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 6
// HOST:   store ptr @.offload_mapnames, ptr %[[VAL_31]], align 8
// HOST:   %[[VAL_32:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 7
// HOST:   store ptr null, ptr %[[VAL_32]], align 8
// HOST:   %[[VAL_33:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 8
// HOST:   store i64 %[[VAL_24]], ptr %[[VAL_33]], align 8
// HOST:   %[[VAL_34:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 9
// HOST:   store i64 0, ptr %[[VAL_34]], align 8
// HOST:   %[[VAL_35:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 10
// HOST:   store [3 x i32] zeroinitializer, ptr %[[VAL_35]], align 4
// HOST:   %[[VAL_36:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 11
// HOST:   store [3 x i32] zeroinitializer, ptr %[[VAL_36]], align 4
// HOST:   %[[VAL_37:.*]] = getelementptr inbounds nuw %[[VAL_5]], ptr %[[VAL_4]], i32 0, i32 12
// HOST:   store i32 0, ptr %[[VAL_37]], align 4
// HOST:   %[[VAL_38:.*]] = call i32 @__tgt_target_kernel(ptr @2, i64 -1, i32 0, i32 0, ptr @.__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}.region_id, ptr %[[VAL_4]])
// HOST:   %[[VAL_39:.*]] = icmp ne i32 %[[VAL_38]], 0
// HOST:   br i1 %[[VAL_39]], label %[[VAL_40:.*]], label %[[VAL_41:.*]]
// HOST: omp_offload.failed:
// HOST:   call void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}(i32 %[[VAL_6]], i32 %[[VAL_6]], i32 %[[VAL_6]], i64 %[[VAL_10]])
// HOST:   br label %[[VAL_41]]
// HOST: omp_offload.cont:
// HOST:   ret void
// HOST: }
// HOST-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}(
// HOST: entry:
// HOST:   %[[VAL_0:.*]] = alloca i64, align 8
// HOST:   store i64 %[[VAL_1:.*]], ptr %[[VAL_0]], align 4
// HOST:   br label %[[VAL_2:.*]]
// HOST: omp.region.after_alloca:
// HOST:   br label %[[VAL_3:.*]]
// HOST: outlined.body:
// HOST:   br label %[[VAL_4:.*]]
// HOST: omp.target:
// HOST:   %[[VAL_5:.*]] = load i32, ptr %[[VAL_0]], align 4
// HOST:   br label %[[VAL_6:.*]]
// HOST: codeRepl:
// HOST:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @2, i32 2, ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.1, i32 %[[VAL_7:.*]], i32 %[[VAL_5]])
// HOST:   br label %[[VAL_8:.*]]
// HOST: teams.exit:
// HOST:   br label %[[VAL_9:.*]]
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
// HOST:   br label %[[VAL_3:.*]]
// HOST: distribute.alloca:
// HOST:   %[[VAL_4:.*]] = alloca i32, align 4
// HOST:   %[[VAL_5:.*]] = alloca i32, align 4
// HOST:   %[[VAL_6:.*]] = alloca i32, align 4
// HOST:   %[[VAL_7:.*]] = alloca i32, align 4
// HOST:   br label %[[VAL_8:.*]]
// HOST: omp.region.after_alloca3:
// HOST:   br label %[[VAL_9:.*]]
// HOST: distribute.body:
// HOST:   br label %[[VAL_10:.*]]
// HOST: omp.distribute.region:
// HOST:   %[[VAL_11:.*]] = icmp slt i32 %[[VAL_12:.*]], 0
// HOST:   %[[VAL_13:.*]] = sub i32 0, %[[VAL_12]]
// HOST:   %[[VAL_14:.*]] = select i1 %[[VAL_11]], i32 %[[VAL_13]], i32 %[[VAL_12]]
// HOST:   %[[VAL_15:.*]] = select i1 %[[VAL_11]], i32 %[[VAL_12]], i32 %[[VAL_12]]
// HOST:   %[[VAL_16:.*]] = select i1 %[[VAL_11]], i32 %[[VAL_12]], i32 %[[VAL_12]]
// HOST:   %[[VAL_17:.*]] = sub nsw i32 %[[VAL_16]], %[[VAL_15]]
// HOST:   %[[VAL_18:.*]] = icmp sle i32 %[[VAL_16]], %[[VAL_15]]
// HOST:   %[[VAL_19:.*]] = sub i32 %[[VAL_17]], 1
// HOST:   %[[VAL_20:.*]] = udiv i32 %[[VAL_19]], %[[VAL_14]]
// HOST:   %[[VAL_21:.*]] = add i32 %[[VAL_20]], 1
// HOST:   %[[VAL_22:.*]] = icmp ule i32 %[[VAL_17]], %[[VAL_14]]
// HOST:   %[[VAL_23:.*]] = select i1 %[[VAL_22]], i32 1, i32 %[[VAL_21]]
// HOST:   %[[VAL_24:.*]] = select i1 %[[VAL_18]], i32 0, i32 %[[VAL_23]]
// HOST:   br label %[[VAL_25:.*]]
// HOST: omp_loop.preheader:
// HOST:   store i32 0, ptr %[[VAL_5]], align 4
// HOST:   %[[VAL_26:.*]] = sub i32 %[[VAL_24]], 1
// HOST:   store i32 %[[VAL_26]], ptr %[[VAL_6]], align 4
// HOST:   store i32 1, ptr %[[VAL_7]], align 4
// HOST:   %[[VAL_27:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// HOST:   call void @__kmpc_for_static_init_4u(ptr @2, i32 %[[VAL_27]], i32 92, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// HOST:   %[[VAL_28:.*]] = load i32, ptr %[[VAL_5]], align 4
// HOST:   %[[VAL_29:.*]] = load i32, ptr %[[VAL_6]], align 4
// HOST:   %[[VAL_30:.*]] = sub i32 %[[VAL_29]], %[[VAL_28]]
// HOST:   %[[VAL_31:.*]] = add i32 %[[VAL_30]], 1
// HOST:   br label %[[VAL_32:.*]]
// HOST: omp_loop.header:
// HOST:   %[[VAL_33:.*]] = phi i32 [ 0, %[[VAL_25]] ], [ %[[VAL_34:.*]], %[[VAL_35:.*]] ]
// HOST:   br label %[[VAL_36:.*]]
// HOST: omp_loop.cond:
// HOST:   %[[VAL_37:.*]] = icmp ult i32 %[[VAL_33]], %[[VAL_31]]
// HOST:   br i1 %[[VAL_37]], label %[[VAL_38:.*]], label %[[VAL_39:.*]]
// HOST: omp_loop.exit:
// HOST:   call void @__kmpc_for_static_fini(ptr @2, i32 %[[VAL_27]])
// HOST:   br label %[[VAL_40:.*]]
// HOST: omp_loop.after:
// HOST:   br label %[[VAL_41:.*]]
// HOST: omp.region.cont4:
// HOST:   br label %[[VAL_42:.*]]
// HOST: distribute.exit:
// HOST:   br label %[[VAL_43:.*]]
// HOST: omp.region.cont2:
// HOST:   br label %[[VAL_44:.*]]
// HOST: omp_loop.body:
// HOST:   %[[VAL_45:.*]] = add i32 %[[VAL_33]], %[[VAL_28]]
// HOST:   %[[VAL_46:.*]] = mul i32 %[[VAL_45]], %[[VAL_12]]
// HOST:   %[[VAL_47:.*]] = add i32 %[[VAL_46]], %[[VAL_12]]
// HOST:   br label %[[VAL_48:.*]]
// HOST: omp.loop_nest.region:
// HOST:   %[[VAL_49:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// HOST:   br label %[[VAL_50:.*]]
// HOST: omp_parallel:
// HOST:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @2, i32 1, ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par, i32 %[[VAL_51:.*]])
// HOST:   br label %[[VAL_52:.*]]
// HOST: omp.par.exit:
// HOST:   br label %[[VAL_53:.*]]
// HOST: omp.region.cont5:
// HOST:   br label %[[VAL_35]]
// HOST: omp_loop.inc:
// HOST:   %[[VAL_34]] = add nuw i32 %[[VAL_33]], 1
// HOST:   br label %[[VAL_32]]
// HOST: teams.exit.exitStub:
// HOST:   ret void
// HOST: }
// HOST-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par(
// HOST: omp.par.entry:
// HOST:   %[[VAL_0:.*]] = alloca i32, align 4
// HOST:   %[[VAL_1:.*]] = alloca i32, align 4
// HOST:   %[[VAL_2:.*]] = alloca i32, align 4
// HOST:   %[[VAL_3:.*]] = alloca i32, align 4
// HOST:   %[[VAL_4:.*]] = alloca i32, align 4
// HOST:   %[[VAL_5:.*]] = load i32, ptr %[[VAL_6:.*]], align 4
// HOST:   store i32 %[[VAL_5]], ptr %[[VAL_4]], align 4
// HOST:   %[[VAL_7:.*]] = load i32, ptr %[[VAL_4]], align 4
// HOST:   br label %[[VAL_8:.*]]
// HOST: omp.region.after_alloca9:
// HOST:   br label %[[VAL_9:.*]]
// HOST: omp.region.after_alloca6:
// HOST:   br label %[[VAL_10:.*]]
// HOST: omp.par.region:
// HOST:   br label %[[VAL_11:.*]]
// HOST: omp.par.region8:
// HOST:   br label %[[VAL_12:.*]]
// HOST: omp.wsloop.region:
// HOST:   %[[VAL_13:.*]] = icmp slt i32 %[[VAL_14:.*]], 0
// HOST:   %[[VAL_15:.*]] = sub i32 0, %[[VAL_14]]
// HOST:   %[[VAL_16:.*]] = select i1 %[[VAL_13]], i32 %[[VAL_15]], i32 %[[VAL_14]]
// HOST:   %[[VAL_17:.*]] = select i1 %[[VAL_13]], i32 %[[VAL_14]], i32 %[[VAL_14]]
// HOST:   %[[VAL_18:.*]] = select i1 %[[VAL_13]], i32 %[[VAL_14]], i32 %[[VAL_14]]
// HOST:   %[[VAL_19:.*]] = sub nsw i32 %[[VAL_18]], %[[VAL_17]]
// HOST:   %[[VAL_20:.*]] = icmp sle i32 %[[VAL_18]], %[[VAL_17]]
// HOST:   %[[VAL_21:.*]] = sub i32 %[[VAL_19]], 1
// HOST:   %[[VAL_22:.*]] = udiv i32 %[[VAL_21]], %[[VAL_16]]
// HOST:   %[[VAL_23:.*]] = add i32 %[[VAL_22]], 1
// HOST:   %[[VAL_24:.*]] = icmp ule i32 %[[VAL_19]], %[[VAL_16]]
// HOST:   %[[VAL_25:.*]] = select i1 %[[VAL_24]], i32 1, i32 %[[VAL_23]]
// HOST:   %[[VAL_26:.*]] = select i1 %[[VAL_20]], i32 0, i32 %[[VAL_25]]
// HOST:   br label %[[VAL_27:.*]]
// HOST: omp_loop.preheader12:
// HOST:   store i32 0, ptr %[[VAL_1]], align 4
// HOST:   %[[VAL_28:.*]] = sub i32 %[[VAL_26]], 1
// HOST:   store i32 %[[VAL_28]], ptr %[[VAL_2]], align 4
// HOST:   store i32 1, ptr %[[VAL_3]], align 4
// HOST:   %[[VAL_29:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// HOST:   call void @__kmpc_for_static_init_4u(ptr @2, i32 %[[VAL_29]], i32 34, ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], i32 1, i32 1)
// HOST:   %[[VAL_30:.*]] = load i32, ptr %[[VAL_1]], align 4
// HOST:   %[[VAL_31:.*]] = load i32, ptr %[[VAL_2]], align 4
// HOST:   %[[VAL_32:.*]] = sub i32 %[[VAL_31]], %[[VAL_30]]
// HOST:   %[[VAL_33:.*]] = add i32 %[[VAL_32]], 1
// HOST:   br label %[[VAL_34:.*]]
// HOST: omp_loop.header13:
// HOST:   %[[VAL_35:.*]] = phi i32 [ 0, %[[VAL_27]] ], [ %[[VAL_36:.*]], %[[VAL_37:.*]] ]
// HOST:   br label %[[VAL_38:.*]]
// HOST: omp_loop.cond14:
// HOST:   %[[VAL_39:.*]] = icmp ult i32 %[[VAL_35]], %[[VAL_33]]
// HOST:   br i1 %[[VAL_39]], label %[[VAL_40:.*]], label %[[VAL_41:.*]]
// HOST: omp_loop.exit17:
// HOST:   call void @__kmpc_for_static_fini(ptr @2, i32 %[[VAL_29]])
// HOST:   call void @__kmpc_barrier(ptr @3, i32 %[[VAL_29]])
// HOST:   br label %[[VAL_42:.*]]
// HOST: omp_loop.after18:
// HOST:   br label %[[VAL_43:.*]]
// HOST: omp.region.cont10:
// HOST:   br label %[[VAL_44:.*]]
// HOST: omp.region.cont7:
// HOST:   br label %[[VAL_45:.*]]
// HOST: omp.par.pre_finalize:
// HOST:   br label %[[VAL_46:.*]]
// HOST: .fini:
// HOST:   br label %[[VAL_47:.*]]
// HOST: omp_loop.body15:
// HOST:   %[[VAL_48:.*]] = add i32 %[[VAL_35]], %[[VAL_30]]
// HOST:   %[[VAL_49:.*]] = mul i32 %[[VAL_48]], %[[VAL_14]]
// HOST:   %[[VAL_50:.*]] = add i32 %[[VAL_49]], %[[VAL_14]]
// HOST:   br label %[[VAL_51:.*]]
// HOST: omp.loop_nest.region23:
// HOST:   br label %[[VAL_52:.*]]
// HOST: omp.region.cont22:
// HOST:   br label %[[VAL_37]]
// HOST: omp_loop.inc16:
// HOST:   %[[VAL_36]] = add nuw i32 %[[VAL_35]], 1
// HOST:   br label %[[VAL_34]]
// HOST: omp.par.exit.exitStub:
// HOST:   ret void
// HOST: }

// DEVICE: target datalayout = "A5"
// DEVICE: target triple = "amdgcn-amd-amdhsa"
// DEVICE: @0 = private unnamed_addr constant [{{.*}} x i8] c";{{.*}}/device.mlir;unknown;{{.*}};{{.*}};;\00", align 1
// DEVICE: @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}_exec_mode = weak protected constant i8 3
// DEVICE: @llvm.compiler.used = appending global [1 x ptr] [ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}_exec_mode], section "llvm.metadata"
// DEVICE: @1 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// DEVICE: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @1 }, align 8
// DEVICE: @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}_dynamic_environment = weak_odr protected global %struct.DynamicEnvironmentTy zeroinitializer
// DEVICE: @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}_kernel_environment = weak_odr protected constant %struct.KernelEnvironmentTy {{[{]}} %struct.ConfigurationEnvironmentTy {{[{]}} i8 1, i8 1, i8 3, i32 1, i32 256, i32 0, i32 0, i32 0, i32 0 }, ptr @2, ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}_dynamic_environment }
// DEVICE-LABEL: define void @main(
// DEVICE:   br label %[[VAL_0:.*]]
// DEVICE: entry:
// DEVICE:   ret void
// DEVICE: }
// DEVICE: define weak_odr protected amdgpu_kernel void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}(ptr %0, ptr %1) #0 {{[{]}}
// DEVICE: entry:
// DEVICE:   %structArg = alloca {{[{]}} i32 }, align 8, addrspace(5)
// DEVICE:   %structArg.ascast = addrspacecast ptr addrspace(5) %structArg to ptr
// DEVICE:   %2 = alloca ptr, align 8, addrspace(5)
// DEVICE:   %3 = addrspacecast ptr addrspace(5) %2 to ptr
// DEVICE:   store ptr %1, ptr %3, align 8
// DEVICE:   %gid.addr = alloca i32, align 4, addrspace(5)
// DEVICE:   %tid.addr25 = alloca i32, align 4, addrspace(5)
// DEVICE:   %4 = call i32 @__kmpc_target_init(ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}_kernel_environment, ptr %0)
// DEVICE:   %exec_user_code = icmp eq i32 %4, -1
// DEVICE:   br i1 %exec_user_code, label %user_code.entry, label %worker.exit
// DEVICE: user_code.entry:
// DEVICE:   br label %omp.region.after_alloca
// DEVICE: omp.region.after_alloca:
// DEVICE:   br label %outlined.body
// DEVICE: outlined.body:
// DEVICE:   br label %omp.target
// DEVICE: omp.target:
// DEVICE:   %5 = load i32, ptr %3, align 4
// DEVICE:   br label %codeRepl
// DEVICE: codeRepl:
// DEVICE:   %gep_ = getelementptr {{[{]}} i32 }, ptr addrspace(5) %structArg, i32 0, i32 0
// DEVICE:   store i32 %5, ptr addrspace(5) %gep_, align 4
// DEVICE:   call void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.4(ptr addrspace(5) %gid.addr, ptr addrspace(5) %tid.addr25, ptr %structArg.ascast)
// DEVICE:   br label %teams.exit
// DEVICE: teams.exit:
// DEVICE:   br label %omp.region.cont
// DEVICE: omp.region.cont:
// DEVICE:   call void @__kmpc_target_deinit()
// DEVICE:   ret void
// DEVICE: worker.exit:
// DEVICE:   ret void
// DEVICE: }
// DEVICE-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.4(
// DEVICE: teams.alloca:
// DEVICE:   %[[VAL_0:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_1:.*]], i32 0, i32 0
// DEVICE:   %[[VAL_2:.*]] = load i32, ptr %[[VAL_0]], align 4
// DEVICE:   %[[VAL_3:.*]] = alloca {{[{]}} i32 }, align 8, addrspace(5)
// DEVICE:   %[[VAL_4:.*]] = addrspacecast ptr addrspace(5) %[[VAL_3]] to ptr
// DEVICE:   br label %[[VAL_5:.*]]
// DEVICE: omp.region.after_alloca1:
// DEVICE:   %[[VAL_6:.*]] = load i32, ptr addrspace(5) %[[VAL_7:.*]], align 4
// DEVICE:   %[[VAL_8:.*]] = load i32, ptr addrspace(5) %[[VAL_9:.*]], align 4
// DEVICE:   br label %[[VAL_10:.*]]
// DEVICE: teams.body:
// DEVICE:   br label %[[VAL_11:.*]]
// DEVICE: omp.teams.region:
// DEVICE:   br label %[[VAL_12:.*]]
// DEVICE: codeRepl:
// DEVICE:   %[[VAL_13:.*]] = getelementptr {{[{]}} i32 }, ptr addrspace(5) %[[VAL_3]], i32 0, i32 0
// DEVICE:   store i32 %[[VAL_2]], ptr addrspace(5) %[[VAL_13]], align 4
// DEVICE:   call void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.3(ptr %[[VAL_4]])
// DEVICE:   br label %[[VAL_14:.*]]
// DEVICE: distribute.exit:
// DEVICE:   br label %[[VAL_15:.*]]
// DEVICE: omp.region.cont2:
// DEVICE:   br label %[[VAL_16:.*]]
// DEVICE: teams.exit.exitStub:
// DEVICE:   ret void
// DEVICE: }
// DEVICE-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.3(
// DEVICE: distribute.alloca:
// DEVICE:   %[[VAL_0:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_1:.*]], i32 0, i32 0
// DEVICE:   %[[VAL_2:.*]] = load i32, ptr %[[VAL_0]], align 4
// DEVICE:   %[[VAL_3:.*]] = alloca {{[{]}} i32, ptr addrspace(5), ptr addrspace(5), ptr, ptr }, align 8, addrspace(5)
// DEVICE:   %[[VAL_4:.*]] = addrspacecast ptr addrspace(5) %[[VAL_3]] to ptr
// DEVICE:   %[[VAL_5:.*]] = alloca [1 x ptr], align 8, addrspace(5)
// DEVICE:   %[[VAL_6:.*]] = addrspacecast ptr addrspace(5) %[[VAL_5]] to ptr
// DEVICE:   %[[VAL_7:.*]] = alloca {{[{]}} ptr addrspace(5) }, align 8, addrspace(5)
// DEVICE:   %[[VAL_8:.*]] = addrspacecast ptr addrspace(5) %[[VAL_7]] to ptr
// DEVICE:   %[[VAL_9:.*]] = alloca i32, align 4, addrspace(5)
// DEVICE:   br label %[[VAL_10:.*]]
// DEVICE: omp.region.after_alloca3:
// DEVICE:   br label %[[VAL_11:.*]]
// DEVICE: distribute.body:
// DEVICE:   br label %[[VAL_12:.*]]
// DEVICE: omp.distribute.region:
// DEVICE:   %[[VAL_13:.*]] = icmp slt i32 %[[VAL_2]], 0
// DEVICE:   %[[VAL_14:.*]] = sub i32 0, %[[VAL_2]]
// DEVICE:   %[[VAL_15:.*]] = select i1 %[[VAL_13]], i32 %[[VAL_14]], i32 %[[VAL_2]]
// DEVICE:   %[[VAL_16:.*]] = select i1 %[[VAL_13]], i32 %[[VAL_2]], i32 %[[VAL_2]]
// DEVICE:   %[[VAL_17:.*]] = select i1 %[[VAL_13]], i32 %[[VAL_2]], i32 %[[VAL_2]]
// DEVICE:   %[[VAL_18:.*]] = sub nsw i32 %[[VAL_17]], %[[VAL_16]]
// DEVICE:   %[[VAL_19:.*]] = icmp sle i32 %[[VAL_17]], %[[VAL_16]]
// DEVICE:   %[[VAL_20:.*]] = sub i32 %[[VAL_18]], 1
// DEVICE:   %[[VAL_21:.*]] = udiv i32 %[[VAL_20]], %[[VAL_15]]
// DEVICE:   %[[VAL_22:.*]] = add i32 %[[VAL_21]], 1
// DEVICE:   %[[VAL_23:.*]] = icmp ule i32 %[[VAL_18]], %[[VAL_15]]
// DEVICE:   %[[VAL_24:.*]] = select i1 %[[VAL_23]], i32 1, i32 %[[VAL_22]]
// DEVICE:   %[[VAL_25:.*]] = select i1 %[[VAL_19]], i32 0, i32 %[[VAL_24]]
// DEVICE:   br label %[[VAL_26:.*]]
// DEVICE: omp_loop.preheader:
// DEVICE:   %[[VAL_27:.*]] = getelementptr {{[{]}} i32, ptr addrspace(5), ptr addrspace(5), ptr, ptr }, ptr addrspace(5) %[[VAL_3]], i32 0, i32 0
// DEVICE:   store i32 %[[VAL_2]], ptr addrspace(5) %[[VAL_27]], align 4
// DEVICE:   %[[VAL_28:.*]] = getelementptr {{[{]}} i32, ptr addrspace(5), ptr addrspace(5), ptr, ptr }, ptr addrspace(5) %[[VAL_3]], i32 0, i32 1
// DEVICE:   store ptr addrspace(5) %[[VAL_9]], ptr addrspace(5) %[[VAL_28]], align 8
// DEVICE:   %[[VAL_29:.*]] = getelementptr {{[{]}} i32, ptr addrspace(5), ptr addrspace(5), ptr, ptr }, ptr addrspace(5) %[[VAL_3]], i32 0, i32 2
// DEVICE:   store ptr addrspace(5) %[[VAL_7]], ptr addrspace(5) %[[VAL_29]], align 8
// DEVICE:   %[[VAL_30:.*]] = getelementptr {{[{]}} i32, ptr addrspace(5), ptr addrspace(5), ptr, ptr }, ptr addrspace(5) %[[VAL_3]], i32 0, i32 3
// DEVICE:   store ptr %[[VAL_6]], ptr addrspace(5) %[[VAL_30]], align 8
// DEVICE:   %[[VAL_31:.*]] = getelementptr {{[{]}} i32, ptr addrspace(5), ptr addrspace(5), ptr, ptr }, ptr addrspace(5) %[[VAL_3]], i32 0, i32 4
// DEVICE:   store ptr %[[VAL_8]], ptr addrspace(5) %[[VAL_31]], align 8
// DEVICE:   call void @__kmpc_distribute_static_loop_4u(ptr @2, ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.2, ptr %[[VAL_4]], i32 %[[VAL_25]], i32 0, i8 0)
// DEVICE:   br label %[[VAL_32:.*]]
// DEVICE: omp_loop.exit:
// DEVICE:   br label %[[VAL_33:.*]]
// DEVICE: omp_loop.after:
// DEVICE:   br label %[[VAL_34:.*]]
// DEVICE: omp.region.cont4:
// DEVICE:   br label %[[VAL_35:.*]]
// DEVICE: distribute.exit.exitStub:
// DEVICE:   ret void
// DEVICE: }
// DEVICE-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.2(
// DEVICE: omp_loop.body:
// DEVICE:   %[[VAL_0:.*]] = getelementptr {{[{]}} i32, ptr addrspace(5), ptr addrspace(5), ptr, ptr }, ptr %[[VAL_1:.*]], i32 0, i32 0
// DEVICE:   %[[VAL_2:.*]] = load i32, ptr %[[VAL_0]], align 4
// DEVICE:   %[[VAL_3:.*]] = getelementptr {{[{]}} i32, ptr addrspace(5), ptr addrspace(5), ptr, ptr }, ptr %[[VAL_1]], i32 0, i32 1
// DEVICE:   %[[VAL_4:.*]] = load ptr addrspace(5), ptr %[[VAL_3]], align 8, !align !1
// DEVICE:   %[[VAL_5:.*]] = getelementptr {{[{]}} i32, ptr addrspace(5), ptr addrspace(5), ptr, ptr }, ptr %[[VAL_1]], i32 0, i32 2
// DEVICE:   %[[VAL_6:.*]] = load ptr addrspace(5), ptr %[[VAL_5]], align 8, !align !2
// DEVICE:   %[[VAL_7:.*]] = getelementptr {{[{]}} i32, ptr addrspace(5), ptr addrspace(5), ptr, ptr }, ptr %[[VAL_1]], i32 0, i32 3
// DEVICE:   %[[VAL_8:.*]] = load ptr, ptr %[[VAL_7]], align 8, !align !2
// DEVICE:   %[[VAL_9:.*]] = getelementptr {{[{]}} i32, ptr addrspace(5), ptr addrspace(5), ptr, ptr }, ptr %[[VAL_1]], i32 0, i32 4
// DEVICE:   %[[VAL_10:.*]] = load ptr, ptr %[[VAL_9]], align 8, !align !2
// DEVICE:   %[[VAL_11:.*]] = mul i32 %[[VAL_12:.*]], %[[VAL_2]]
// DEVICE:   %[[VAL_13:.*]] = add i32 %[[VAL_11]], %[[VAL_2]]
// DEVICE:   br label %[[VAL_14:.*]]
// DEVICE: omp.loop_nest.region:
// DEVICE:   %[[VAL_15:.*]] = call i32 @__kmpc_global_thread_num(ptr @2)
// DEVICE:   store i32 %[[VAL_2]], ptr addrspace(5) %[[VAL_4]], align 4
// DEVICE:   br label %[[VAL_16:.*]]
// DEVICE: omp_parallel:
// DEVICE:   %[[VAL_17:.*]] = getelementptr {{[{]}} ptr addrspace(5) }, ptr addrspace(5) %[[VAL_6]], i32 0, i32 0
// DEVICE:   store ptr addrspace(5) %[[VAL_4]], ptr addrspace(5) %[[VAL_17]], align 8
// DEVICE:   %[[VAL_18:.*]] = getelementptr inbounds [1 x ptr], ptr %[[VAL_8]], i64 0, i64 0
// DEVICE:   store ptr %[[VAL_10]], ptr %[[VAL_18]], align 8
// DEVICE:   call void @__kmpc_parallel_60(ptr @2, i32 %[[VAL_15]], i32 1, i32 -1, i32 -1, ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.1, ptr null, ptr %[[VAL_8]], i64 1, i32 0)
// DEVICE:   br label %[[VAL_19:.*]]
// DEVICE: omp.par.exit:
// DEVICE:   br label %[[VAL_20:.*]]
// DEVICE: omp.region.cont5:
// DEVICE:   br label %[[VAL_21:.*]]
// DEVICE: omp.prelatch24.exitStub:
// DEVICE:   ret void
// DEVICE: }
// DEVICE-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par.1(
// DEVICE: omp.par.entry:
// DEVICE:   %[[VAL_0:.*]] = getelementptr {{[{]}} ptr addrspace(5) }, ptr %[[VAL_1:.*]], i32 0, i32 0
// DEVICE:   %[[VAL_2:.*]] = load ptr addrspace(5), ptr %[[VAL_0]], align 8, !align !1
// DEVICE:   %[[VAL_3:.*]] = alloca {{[{]}} i32 }, align 8, addrspace(5)
// DEVICE:   %[[VAL_4:.*]] = addrspacecast ptr addrspace(5) %[[VAL_3]] to ptr
// DEVICE:   %[[VAL_5:.*]] = alloca i32, align 4, addrspace(5)
// DEVICE:   %[[VAL_6:.*]] = load i32, ptr %[[VAL_7:.*]], align 4
// DEVICE:   store i32 %[[VAL_6]], ptr addrspace(5) %[[VAL_5]], align 4
// DEVICE:   %[[VAL_8:.*]] = load i32, ptr addrspace(5) %[[VAL_5]], align 4
// DEVICE:   %[[VAL_9:.*]] = load i32, ptr addrspace(5) %[[VAL_2]], align 4
// DEVICE:   br label %[[VAL_10:.*]]
// DEVICE: omp.region.after_alloca9:
// DEVICE:   br label %[[VAL_11:.*]]
// DEVICE: omp.region.after_alloca6:
// DEVICE:   br label %[[VAL_12:.*]]
// DEVICE: omp.par.region:
// DEVICE:   br label %[[VAL_13:.*]]
// DEVICE: omp.par.region8:
// DEVICE:   br label %[[VAL_14:.*]]
// DEVICE: omp.wsloop.region:
// DEVICE:   %[[VAL_15:.*]] = icmp slt i32 %[[VAL_9]], 0
// DEVICE:   %[[VAL_16:.*]] = sub i32 0, %[[VAL_9]]
// DEVICE:   %[[VAL_17:.*]] = select i1 %[[VAL_15]], i32 %[[VAL_16]], i32 %[[VAL_9]]
// DEVICE:   %[[VAL_18:.*]] = select i1 %[[VAL_15]], i32 %[[VAL_9]], i32 %[[VAL_9]]
// DEVICE:   %[[VAL_19:.*]] = select i1 %[[VAL_15]], i32 %[[VAL_9]], i32 %[[VAL_9]]
// DEVICE:   %[[VAL_20:.*]] = sub nsw i32 %[[VAL_19]], %[[VAL_18]]
// DEVICE:   %[[VAL_21:.*]] = icmp sle i32 %[[VAL_19]], %[[VAL_18]]
// DEVICE:   %[[VAL_22:.*]] = sub i32 %[[VAL_20]], 1
// DEVICE:   %[[VAL_23:.*]] = udiv i32 %[[VAL_22]], %[[VAL_17]]
// DEVICE:   %[[VAL_24:.*]] = add i32 %[[VAL_23]], 1
// DEVICE:   %[[VAL_25:.*]] = icmp ule i32 %[[VAL_20]], %[[VAL_17]]
// DEVICE:   %[[VAL_26:.*]] = select i1 %[[VAL_25]], i32 1, i32 %[[VAL_24]]
// DEVICE:   %[[VAL_27:.*]] = select i1 %[[VAL_21]], i32 0, i32 %[[VAL_26]]
// DEVICE:   br label %[[VAL_28:.*]]
// DEVICE: omp_loop.preheader12:
// DEVICE:   %[[VAL_29:.*]] = getelementptr {{[{]}} i32 }, ptr addrspace(5) %[[VAL_3]], i32 0, i32 0
// DEVICE:   store i32 %[[VAL_9]], ptr addrspace(5) %[[VAL_29]], align 4
// DEVICE:   %[[VAL_30:.*]] = call i32 @omp_get_num_threads()
// DEVICE:   call void @__kmpc_for_static_loop_4u(ptr @2, ptr @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par, ptr %[[VAL_4]], i32 %[[VAL_27]], i32 %[[VAL_30]], i32 0, i8 0)
// DEVICE:   br label %[[VAL_31:.*]]
// DEVICE: omp_loop.exit17:
// DEVICE:   br label %[[VAL_32:.*]]
// DEVICE: omp_loop.after18:
// DEVICE:   br label %[[VAL_33:.*]]
// DEVICE: omp.region.cont10:
// DEVICE:   br label %[[VAL_34:.*]]
// DEVICE: omp.region.cont7:
// DEVICE:   br label %[[VAL_35:.*]]
// DEVICE: omp.par.pre_finalize:
// DEVICE:   br label %[[VAL_36:.*]]
// DEVICE: .fini:
// DEVICE:   br label %[[VAL_37:.*]]
// DEVICE: omp.par.exit.exitStub:
// DEVICE:   ret void
// DEVICE: }
// DEVICE-LABEL: define internal void @__omp_offloading_{{.*}}_{{.*}}_main_l{{.*}}..omp_par(
// DEVICE: omp_loop.body15:
// DEVICE:   %[[VAL_0:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_1:.*]], i32 0, i32 0
// DEVICE:   %[[VAL_2:.*]] = load i32, ptr %[[VAL_0]], align 4
// DEVICE:   %[[VAL_3:.*]] = mul i32 %[[VAL_4:.*]], %[[VAL_2]]
// DEVICE:   %[[VAL_5:.*]] = add i32 %[[VAL_3]], %[[VAL_2]]
// DEVICE:   br label %[[VAL_6:.*]]
// DEVICE: omp.loop_nest.region23:
// DEVICE:   br label %[[VAL_7:.*]]
// DEVICE: omp.region.cont22:
// DEVICE:   br label %[[VAL_8:.*]]
// DEVICE: omp.prelatch.exitStub:
// DEVICE:   ret void
// DEVICE: }
