// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

llvm.func @cancel_distribute_parallel_do(%lb : i32, %ub : i32, %step : i32) {
  omp.teams {
    omp.parallel {
      omp.distribute {
        omp.wsloop {
          omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
            omp.cancel cancellation_construct_type(loop)
            omp.yield
          }
        } {omp.composite}
      } {omp.composite}
      omp.terminator
    } {omp.composite}
    omp.terminator
  }
  llvm.return
}
// [...]

// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @cancel_distribute_parallel_do(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 3, ptr @cancel_distribute_parallel_do..omp_par.1, i32 %[[VAL_2:.*]], i32 %[[VAL_3:.*]], i32 %[[VAL_4:.*]])
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: teams.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @cancel_distribute_parallel_do..omp_par.1(
// CHECK: teams.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: teams.body:
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp.teams.region:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 3, ptr @cancel_distribute_parallel_do..omp_par, i32 %[[VAL_5:.*]], i32 %[[VAL_6:.*]], i32 %[[VAL_7:.*]])
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @cancel_distribute_parallel_do..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_6:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_7:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.after_alloca1:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region3:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: distribute.alloca:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.after_alloca6:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.after_alloca4:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_17:.*]] = icmp slt i32 %[[VAL_18:.*]], 0
// CHECK:   %[[VAL_19:.*]] = sub i32 0, %[[VAL_18]]
// CHECK:   %[[VAL_20:.*]] = select i1 %[[VAL_17]], i32 %[[VAL_19]], i32 %[[VAL_18]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_17]], i32 %[[VAL_22:.*]], i32 %[[VAL_23:.*]]
// CHECK:   %[[VAL_24:.*]] = select i1 %[[VAL_17]], i32 %[[VAL_23]], i32 %[[VAL_22]]
// CHECK:   %[[VAL_25:.*]] = sub nsw i32 %[[VAL_24]], %[[VAL_21]]
// CHECK:   %[[VAL_26:.*]] = icmp sle i32 %[[VAL_24]], %[[VAL_21]]
// CHECK:   %[[VAL_27:.*]] = sub i32 %[[VAL_25]], 1
// CHECK:   %[[VAL_28:.*]] = udiv i32 %[[VAL_27]], %[[VAL_20]]
// CHECK:   %[[VAL_29:.*]] = add i32 %[[VAL_28]], 1
// CHECK:   %[[VAL_30:.*]] = icmp ule i32 %[[VAL_25]], %[[VAL_20]]
// CHECK:   %[[VAL_31:.*]] = select i1 %[[VAL_30]], i32 1, i32 %[[VAL_29]]
// CHECK:   %[[VAL_32:.*]] = select i1 %[[VAL_26]], i32 0, i32 %[[VAL_31]]
// CHECK:   br label %[[VAL_33:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_34:.*]] = sub i32 %[[VAL_32]], 1
// CHECK:   store i32 %[[VAL_34]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_35:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_35]], i32 34, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   %[[VAL_36:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_37:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_38:.*]] = sub i32 %[[VAL_37]], %[[VAL_36]]
// CHECK:   %[[VAL_39:.*]] = add i32 %[[VAL_38]], 1
// CHECK:   br label %[[VAL_40:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_41:.*]] = phi i32 [ 0, %[[VAL_33]] ], [ %[[VAL_42:.*]], %[[VAL_43:.*]] ]
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_45:.*]] = icmp ult i32 %[[VAL_41]], %[[VAL_39]]
// CHECK:   br i1 %[[VAL_45]], label %[[VAL_46:.*]], label %[[VAL_47:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_35]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_35]])
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_49:.*]]
// CHECK: omp.region.cont7:
// CHECK:   br label %[[VAL_50:.*]]
// CHECK: omp.region.cont5:
// CHECK:   br label %[[VAL_51:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_52:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_53:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_54:.*]]
// CHECK: .fini11:
// CHECK:   %[[VAL_55:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_55]])
// CHECK:   br label %[[VAL_56:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_57:.*]] = add i32 %[[VAL_41]], %[[VAL_36]]
// CHECK:   %[[VAL_58:.*]] = mul i32 %[[VAL_57]], %[[VAL_18]]
// CHECK:   %[[VAL_59:.*]] = add i32 %[[VAL_58]], %[[VAL_23]]
// CHECK:   br label %[[VAL_60:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   %[[VAL_61:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_62:.*]] = call i32 @__kmpc_cancel(ptr @1, i32 %[[VAL_61]], i32 2)
// CHECK:   %[[VAL_63:.*]] = icmp eq i32 %[[VAL_62]], 0
// CHECK:   br i1 %[[VAL_63]], label %[[VAL_64:.*]], label %[[VAL_65:.*]]
// CHECK: omp.loop_nest.region.cncl:
// CHECK:   br label %[[VAL_66:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_48]]
// CHECK: omp.loop_nest.region.split:
// CHECK:   br label %[[VAL_67:.*]]
// CHECK: omp.region.cont8:
// CHECK:   br label %[[VAL_43]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_42]] = add nuw i32 %[[VAL_41]], 1
// CHECK:   br label %[[VAL_40]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
