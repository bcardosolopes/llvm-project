// Test that dist_schedule gets correctly translated with the correct schedule type and chunk size where appropriate while using workshare loops.

// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s

llvm.func @distribute_wsloop_dist_schedule_chunked_schedule_chunked(%n: i32, %teams: i32, %threads: i32, %dcs: i32) {
  %0 = llvm.mlir.constant(0 : i32) : i32
  %1 = llvm.mlir.constant(1 : i32) : i32
  %scs = llvm.mlir.constant(64 : i32) : i32

  omp.teams num_teams(to %teams : i32) thread_limit(%threads : i32) {
    omp.parallel {
      omp.distribute dist_schedule_static dist_schedule_chunk_size(%dcs : i32) {
        omp.wsloop schedule(static = %scs : i32) {
          omp.loop_nest (%i) : i32 = (%0) to (%n) step (%1) {
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

llvm.func @distribute_wsloop_dist_schedule_chunked_schedule_chunked_i64(%n: i32, %teams: i32, %threads: i32) {
  %0 = llvm.mlir.constant(0 : i64) : i64
  %1 = llvm.mlir.constant(1 : i64) : i64
  %dcs = llvm.mlir.constant(1024 : i64) : i64
  %scs = llvm.mlir.constant(64 : i64) : i64
  %n64 = llvm.zext %n : i32 to i64

  omp.teams num_teams(to %teams : i32) thread_limit(%threads : i32) {
    omp.parallel {
      omp.distribute dist_schedule_static dist_schedule_chunk_size(%dcs : i64) {
        omp.wsloop schedule(static = %scs : i64) {
          omp.loop_nest (%i) : i64 = (%0) to (%n64) step (%1) {
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

// -----

llvm.func @distribute_wsloop_dist_schedule_chunked(%n: i32, %teams: i32, %threads: i32) {
  %0 = llvm.mlir.constant(0 : i32) : i32
  %1 = llvm.mlir.constant(1 : i32) : i32
  %dcs = llvm.mlir.constant(1024 : i32) : i32

  omp.teams num_teams(to %teams : i32) thread_limit(%threads : i32) {
    omp.parallel {
      omp.distribute dist_schedule_static dist_schedule_chunk_size(%dcs : i32) {
        omp.wsloop schedule(static) {
          omp.loop_nest (%i) : i32 = (%0) to (%n) step (%1) {
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

llvm.func @distribute_wsloop_dist_schedule_chunked_i64(%n: i32, %teams: i32, %threads: i32) {
  %0 = llvm.mlir.constant(0 : i64) : i64
  %1 = llvm.mlir.constant(1 : i64) : i64
  %dcs = llvm.mlir.constant(1024 : i64) : i64
  %n64 = llvm.zext %n : i32 to i64

  omp.teams num_teams(to %teams : i32) thread_limit(%threads : i32) {
    omp.parallel {
      omp.distribute dist_schedule_static dist_schedule_chunk_size(%dcs : i64) {
        omp.wsloop schedule(static) {
          omp.loop_nest (%i) : i64 = (%0) to (%n64) step (%1) {
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

// -----

llvm.func @distribute_wsloop_schedule_chunked(%n: i32, %teams: i32, %threads: i32) {
  %0 = llvm.mlir.constant(0 : i32) : i32
  %1 = llvm.mlir.constant(1 : i32) : i32
  %scs = llvm.mlir.constant(64 : i32) : i32

  omp.teams num_teams(to %teams : i32) thread_limit(%threads : i32) {
    omp.parallel {
      omp.distribute dist_schedule_static {
        omp.wsloop schedule(static = %scs : i32) {
          omp.loop_nest (%i) : i32 = (%0) to (%n) step (%1) {
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

llvm.func @distribute_wsloop_schedule_chunked_i64(%n: i32, %teams: i32, %threads: i32) {
  %0 = llvm.mlir.constant(0 : i64) : i64
  %1 = llvm.mlir.constant(1 : i64) : i64
  %scs = llvm.mlir.constant(64 : i64) : i64
  %n64 = llvm.zext %n : i32 to i64

  omp.teams num_teams(to %teams : i32) thread_limit(%threads : i32) {
    omp.parallel {
      omp.distribute dist_schedule_static {
        omp.wsloop schedule(static = %scs : i64) {
          omp.loop_nest (%i) : i64 = (%0) to (%n64) step (%1) {
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


// -----

llvm.func @distribute_wsloop_no_chunks(%n: i32, %teams: i32, %threads: i32) {
  %0 = llvm.mlir.constant(0 : i32) : i32
  %1 = llvm.mlir.constant(1 : i32) : i32

  omp.teams num_teams(to %teams : i32) thread_limit(%threads : i32) {
    omp.parallel {
      omp.distribute dist_schedule_static {
        omp.wsloop schedule(static) {
          omp.loop_nest (%i) : i32 = (%0) to (%n) step (%1) {
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

llvm.func @distribute_wsloop_no_chunks_i64(%n: i32, %teams: i32, %threads: i32) {
  %0 = llvm.mlir.constant(0 : i64) : i64
  %1 = llvm.mlir.constant(1 : i64) : i64
  %n64 = llvm.zext %n : i32 to i64

  omp.teams num_teams(to %teams : i32) thread_limit(%threads : i32) {
    omp.parallel {
      omp.distribute dist_schedule_static {
        omp.wsloop schedule(static) {
          omp.loop_nest (%i) : i64 = (%0) to (%n64) step (%1) {
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

// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @distribute_wsloop_dist_schedule_chunked_schedule_chunked(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_push_num_teams_51(ptr @1, i32 %[[VAL_1]], i32 %[[VAL_2:.*]], i32 %[[VAL_2]], i32 %[[VAL_3:.*]])
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 2, ptr @distribute_wsloop_dist_schedule_chunked_schedule_chunked..omp_par.1, i32 %[[VAL_5:.*]], i32 %[[VAL_6:.*]])
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: teams.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_dist_schedule_chunked_schedule_chunked..omp_par.1(
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
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 2, ptr @distribute_wsloop_dist_schedule_chunked_schedule_chunked..omp_par, i32 %[[VAL_5:.*]], i32 %[[VAL_6:.*]])
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_dist_schedule_chunked_schedule_chunked..omp_par(
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
// CHECK: omp.region.after_alloca2:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region4:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: distribute.alloca:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.after_alloca7:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.after_alloca5:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_17:.*]] = select i1 false, i32 %[[VAL_18:.*]], i32 0
// CHECK:   %[[VAL_19:.*]] = select i1 false, i32 0, i32 %[[VAL_18]]
// CHECK:   %[[VAL_20:.*]] = sub nsw i32 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_21:.*]] = icmp sle i32 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_22:.*]] = sub i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_23:.*]] = udiv i32 %[[VAL_22]], 1
// CHECK:   %[[VAL_24:.*]] = add i32 %[[VAL_23]], 1
// CHECK:   %[[VAL_25:.*]] = icmp ule i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_26:.*]] = select i1 %[[VAL_25]], i32 1, i32 %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = select i1 %[[VAL_21]], i32 0, i32 %[[VAL_26]]
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_29:.*]] = sub i32 %[[VAL_27]], 1
// CHECK:   %[[VAL_30:.*]] = icmp eq i32 %[[VAL_27]], 0
// CHECK:   %[[VAL_31:.*]] = select i1 %[[VAL_30]], i32 0, i32 %[[VAL_29]]
// CHECK:   store i32 %[[VAL_31]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_32:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_32]], i32 33, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 64)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_32]], i32 91, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 %[[VAL_33:.*]])
// CHECK:   %[[VAL_34:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_35:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_36:.*]] = add i32 %[[VAL_35]], 1
// CHECK:   %[[VAL_37:.*]] = sub i32 %[[VAL_36]], %[[VAL_34]]
// CHECK:   %[[VAL_38:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_39:.*]] = sub nuw i32 %[[VAL_27]], %[[VAL_34]]
// CHECK:   %[[VAL_40:.*]] = icmp ule i32 %[[VAL_27]], %[[VAL_34]]
// CHECK:   %[[VAL_41:.*]] = sub i32 %[[VAL_39]], 1
// CHECK:   %[[VAL_42:.*]] = udiv i32 %[[VAL_41]], %[[VAL_38]]
// CHECK:   %[[VAL_43:.*]] = add i32 %[[VAL_42]], 1
// CHECK:   %[[VAL_44:.*]] = icmp ule i32 %[[VAL_39]], %[[VAL_38]]
// CHECK:   %[[VAL_45:.*]] = select i1 %[[VAL_44]], i32 1, i32 %[[VAL_43]]
// CHECK:   %[[VAL_46:.*]] = select i1 %[[VAL_40]], i32 0, i32 %[[VAL_45]]
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp_dispatch.preheader:
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: omp_dispatch.header:
// CHECK:   %[[VAL_49:.*]] = phi i32 [ 0, %[[VAL_47]] ], [ %[[VAL_50:.*]], %[[VAL_51:.*]] ]
// CHECK:   br label %[[VAL_52:.*]]
// CHECK: omp_dispatch.cond:
// CHECK:   %[[VAL_53:.*]] = icmp ult i32 %[[VAL_49]], %[[VAL_46]]
// CHECK:   br i1 %[[VAL_53]], label %[[VAL_54:.*]], label %[[VAL_55:.*]]
// CHECK: omp_dispatch.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_32]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_32]])
// CHECK:   br label %[[VAL_56:.*]]
// CHECK: omp_dispatch.after:
// CHECK:   br label %[[VAL_57:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_58:.*]]
// CHECK: omp.region.cont8:
// CHECK:   br label %[[VAL_59:.*]]
// CHECK: omp.region.cont6:
// CHECK:   br label %[[VAL_60:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_61:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_62:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_63:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_64:.*]]
// CHECK: omp_dispatch.body:
// CHECK:   %[[VAL_65:.*]] = mul i32 %[[VAL_49]], %[[VAL_38]]
// CHECK:   %[[VAL_66:.*]] = add i32 %[[VAL_65]], %[[VAL_34]]
// CHECK:   br label %[[VAL_67:.*]]
// CHECK: omp_loop.preheader11:
// CHECK:   %[[VAL_68:.*]] = add i32 %[[VAL_66]], %[[VAL_37]]
// CHECK:   %[[VAL_69:.*]] = icmp uge i32 %[[VAL_68]], %[[VAL_27]]
// CHECK:   %[[VAL_70:.*]] = sub i32 %[[VAL_27]], %[[VAL_66]]
// CHECK:   %[[VAL_71:.*]] = select i1 %[[VAL_69]], i32 %[[VAL_70]], i32 %[[VAL_37]]
// CHECK:   br label %[[VAL_72:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_73:.*]] = phi i32 [ 0, %[[VAL_67]] ], [ %[[VAL_74:.*]], %[[VAL_75:.*]] ]
// CHECK:   br label %[[VAL_76:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_77:.*]] = icmp ult i32 %[[VAL_73]], %[[VAL_71]]
// CHECK:   br i1 %[[VAL_77]], label %[[VAL_78:.*]], label %[[VAL_79:.*]]
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_51]]
// CHECK: omp_dispatch.inc:
// CHECK:   %[[VAL_50]] = add nuw i32 %[[VAL_49]], 1
// CHECK:   br label %[[VAL_48]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_80:.*]] = add i32 %[[VAL_73]], %[[VAL_66]]
// CHECK:   %[[VAL_81:.*]] = mul i32 %[[VAL_80]], 1
// CHECK:   %[[VAL_82:.*]] = add i32 %[[VAL_81]], 0
// CHECK:   br label %[[VAL_83:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_84:.*]]
// CHECK: omp.region.cont9:
// CHECK:   br label %[[VAL_75]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_74]] = add nuw i32 %[[VAL_73]], 1
// CHECK:   br label %[[VAL_72]], !llvm.loop !1
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @distribute_wsloop_dist_schedule_chunked_schedule_chunked_i64(
// CHECK:   %[[VAL_0:.*]] = zext i32 %[[VAL_1:.*]] to i64
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_push_num_teams_51(ptr @1, i32 %[[VAL_3]], i32 %[[VAL_4:.*]], i32 %[[VAL_4]], i32 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 1, ptr @distribute_wsloop_dist_schedule_chunked_schedule_chunked_i64..omp_par.2, i64 %[[VAL_0]])
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: teams.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_dist_schedule_chunked_schedule_chunked_i64..omp_par.2(
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
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @distribute_wsloop_dist_schedule_chunked_schedule_chunked_i64..omp_par, i64 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_dist_schedule_chunked_schedule_chunked_i64..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_6:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_7:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.after_alloca2:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region4:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: distribute.alloca:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.after_alloca7:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.after_alloca5:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_17:.*]] = select i1 false, i64 %[[VAL_18:.*]], i64 0
// CHECK:   %[[VAL_19:.*]] = select i1 false, i64 0, i64 %[[VAL_18]]
// CHECK:   %[[VAL_20:.*]] = sub nsw i64 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_21:.*]] = icmp sle i64 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_22:.*]] = sub i64 %[[VAL_20]], 1
// CHECK:   %[[VAL_23:.*]] = udiv i64 %[[VAL_22]], 1
// CHECK:   %[[VAL_24:.*]] = add i64 %[[VAL_23]], 1
// CHECK:   %[[VAL_25:.*]] = icmp ule i64 %[[VAL_20]], 1
// CHECK:   %[[VAL_26:.*]] = select i1 %[[VAL_25]], i64 1, i64 %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = select i1 %[[VAL_21]], i64 0, i64 %[[VAL_26]]
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_29:.*]] = sub i64 %[[VAL_27]], 1
// CHECK:   %[[VAL_30:.*]] = icmp eq i64 %[[VAL_27]], 0
// CHECK:   %[[VAL_31:.*]] = select i1 %[[VAL_30]], i64 0, i64 %[[VAL_29]]
// CHECK:   store i64 %[[VAL_31]], ptr %[[VAL_6]], align 4
// CHECK:   store i64 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_32:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_8u(ptr @1, i32 %[[VAL_32]], i32 33, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i64 1, i64 64)
// CHECK:   call void @__kmpc_for_static_init_8u(ptr @1, i32 %[[VAL_32]], i32 91, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i64 1, i64 1024)
// CHECK:   %[[VAL_33:.*]] = load i64, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_34:.*]] = load i64, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], 1
// CHECK:   %[[VAL_36:.*]] = sub i64 %[[VAL_35]], %[[VAL_33]]
// CHECK:   %[[VAL_37:.*]] = load i64, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_38:.*]] = sub nuw i64 %[[VAL_27]], %[[VAL_33]]
// CHECK:   %[[VAL_39:.*]] = icmp ule i64 %[[VAL_27]], %[[VAL_33]]
// CHECK:   %[[VAL_40:.*]] = sub i64 %[[VAL_38]], 1
// CHECK:   %[[VAL_41:.*]] = udiv i64 %[[VAL_40]], %[[VAL_37]]
// CHECK:   %[[VAL_42:.*]] = add i64 %[[VAL_41]], 1
// CHECK:   %[[VAL_43:.*]] = icmp ule i64 %[[VAL_38]], %[[VAL_37]]
// CHECK:   %[[VAL_44:.*]] = select i1 %[[VAL_43]], i64 1, i64 %[[VAL_42]]
// CHECK:   %[[VAL_45:.*]] = select i1 %[[VAL_39]], i64 0, i64 %[[VAL_44]]
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: omp_dispatch.preheader:
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp_dispatch.header:
// CHECK:   %[[VAL_48:.*]] = phi i64 [ 0, %[[VAL_46]] ], [ %[[VAL_49:.*]], %[[VAL_50:.*]] ]
// CHECK:   br label %[[VAL_51:.*]]
// CHECK: omp_dispatch.cond:
// CHECK:   %[[VAL_52:.*]] = icmp ult i64 %[[VAL_48]], %[[VAL_45]]
// CHECK:   br i1 %[[VAL_52]], label %[[VAL_53:.*]], label %[[VAL_54:.*]]
// CHECK: omp_dispatch.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_32]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_32]])
// CHECK:   br label %[[VAL_55:.*]]
// CHECK: omp_dispatch.after:
// CHECK:   br label %[[VAL_56:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_57:.*]]
// CHECK: omp.region.cont8:
// CHECK:   br label %[[VAL_58:.*]]
// CHECK: omp.region.cont6:
// CHECK:   br label %[[VAL_59:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_60:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_61:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_62:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_63:.*]]
// CHECK: omp_dispatch.body:
// CHECK:   %[[VAL_64:.*]] = mul i64 %[[VAL_48]], %[[VAL_37]]
// CHECK:   %[[VAL_65:.*]] = add i64 %[[VAL_64]], %[[VAL_33]]
// CHECK:   br label %[[VAL_66:.*]]
// CHECK: omp_loop.preheader11:
// CHECK:   %[[VAL_67:.*]] = add i64 %[[VAL_65]], %[[VAL_36]]
// CHECK:   %[[VAL_68:.*]] = icmp uge i64 %[[VAL_67]], %[[VAL_27]]
// CHECK:   %[[VAL_69:.*]] = sub i64 %[[VAL_27]], %[[VAL_65]]
// CHECK:   %[[VAL_70:.*]] = select i1 %[[VAL_68]], i64 %[[VAL_69]], i64 %[[VAL_36]]
// CHECK:   br label %[[VAL_71:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_72:.*]] = phi i64 [ 0, %[[VAL_66]] ], [ %[[VAL_73:.*]], %[[VAL_74:.*]] ]
// CHECK:   br label %[[VAL_75:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_76:.*]] = icmp ult i64 %[[VAL_72]], %[[VAL_70]]
// CHECK:   br i1 %[[VAL_76]], label %[[VAL_77:.*]], label %[[VAL_78:.*]]
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_50]]
// CHECK: omp_dispatch.inc:
// CHECK:   %[[VAL_49]] = add nuw i64 %[[VAL_48]], 1
// CHECK:   br label %[[VAL_47]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_79:.*]] = add i64 %[[VAL_72]], %[[VAL_65]]
// CHECK:   %[[VAL_80:.*]] = mul i64 %[[VAL_79]], 1
// CHECK:   %[[VAL_81:.*]] = add i64 %[[VAL_80]], 0
// CHECK:   br label %[[VAL_82:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_83:.*]]
// CHECK: omp.region.cont9:
// CHECK:   br label %[[VAL_74]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_73]] = add nuw i64 %[[VAL_72]], 1
// CHECK:   br label %[[VAL_71]], !llvm.loop !4
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @distribute_wsloop_dist_schedule_chunked(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_push_num_teams_51(ptr @1, i32 %[[VAL_1]], i32 %[[VAL_2:.*]], i32 %[[VAL_2]], i32 %[[VAL_3:.*]])
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 1, ptr @distribute_wsloop_dist_schedule_chunked..omp_par.1, i32 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: teams.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_dist_schedule_chunked..omp_par.1(
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
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @distribute_wsloop_dist_schedule_chunked..omp_par, i32 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_dist_schedule_chunked..omp_par(
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
// CHECK: omp.region.after_alloca2:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region4:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: distribute.alloca:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.after_alloca7:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.after_alloca5:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_17:.*]] = select i1 false, i32 %[[VAL_18:.*]], i32 0
// CHECK:   %[[VAL_19:.*]] = select i1 false, i32 0, i32 %[[VAL_18]]
// CHECK:   %[[VAL_20:.*]] = sub nsw i32 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_21:.*]] = icmp sle i32 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_22:.*]] = sub i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_23:.*]] = udiv i32 %[[VAL_22]], 1
// CHECK:   %[[VAL_24:.*]] = add i32 %[[VAL_23]], 1
// CHECK:   %[[VAL_25:.*]] = icmp ule i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_26:.*]] = select i1 %[[VAL_25]], i32 1, i32 %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = select i1 %[[VAL_21]], i32 0, i32 %[[VAL_26]]
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_29:.*]] = sub i32 %[[VAL_27]], 1
// CHECK:   %[[VAL_30:.*]] = icmp eq i32 %[[VAL_27]], 0
// CHECK:   %[[VAL_31:.*]] = select i1 %[[VAL_30]], i32 0, i32 %[[VAL_29]]
// CHECK:   store i32 %[[VAL_31]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_32:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_32]], i32 34, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_32]], i32 91, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1024)
// CHECK:   %[[VAL_33:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_34:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_35:.*]] = add i32 %[[VAL_34]], 1
// CHECK:   %[[VAL_36:.*]] = sub i32 %[[VAL_35]], %[[VAL_33]]
// CHECK:   %[[VAL_37:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_38:.*]] = sub nuw i32 %[[VAL_27]], %[[VAL_33]]
// CHECK:   %[[VAL_39:.*]] = icmp ule i32 %[[VAL_27]], %[[VAL_33]]
// CHECK:   %[[VAL_40:.*]] = sub i32 %[[VAL_38]], 1
// CHECK:   %[[VAL_41:.*]] = udiv i32 %[[VAL_40]], %[[VAL_37]]
// CHECK:   %[[VAL_42:.*]] = add i32 %[[VAL_41]], 1
// CHECK:   %[[VAL_43:.*]] = icmp ule i32 %[[VAL_38]], %[[VAL_37]]
// CHECK:   %[[VAL_44:.*]] = select i1 %[[VAL_43]], i32 1, i32 %[[VAL_42]]
// CHECK:   %[[VAL_45:.*]] = select i1 %[[VAL_39]], i32 0, i32 %[[VAL_44]]
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: omp_dispatch.preheader:
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp_dispatch.header:
// CHECK:   %[[VAL_48:.*]] = phi i32 [ 0, %[[VAL_46]] ], [ %[[VAL_49:.*]], %[[VAL_50:.*]] ]
// CHECK:   br label %[[VAL_51:.*]]
// CHECK: omp_dispatch.cond:
// CHECK:   %[[VAL_52:.*]] = icmp ult i32 %[[VAL_48]], %[[VAL_45]]
// CHECK:   br i1 %[[VAL_52]], label %[[VAL_53:.*]], label %[[VAL_54:.*]]
// CHECK: omp_dispatch.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_32]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_32]])
// CHECK:   br label %[[VAL_55:.*]]
// CHECK: omp_dispatch.after:
// CHECK:   br label %[[VAL_56:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_57:.*]]
// CHECK: omp.region.cont8:
// CHECK:   br label %[[VAL_58:.*]]
// CHECK: omp.region.cont6:
// CHECK:   br label %[[VAL_59:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_60:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_61:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_62:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_63:.*]]
// CHECK: omp_dispatch.body:
// CHECK:   %[[VAL_64:.*]] = mul i32 %[[VAL_48]], %[[VAL_37]]
// CHECK:   %[[VAL_65:.*]] = add i32 %[[VAL_64]], %[[VAL_33]]
// CHECK:   br label %[[VAL_66:.*]]
// CHECK: omp_loop.preheader11:
// CHECK:   %[[VAL_67:.*]] = add i32 %[[VAL_65]], %[[VAL_36]]
// CHECK:   %[[VAL_68:.*]] = icmp uge i32 %[[VAL_67]], %[[VAL_27]]
// CHECK:   %[[VAL_69:.*]] = sub i32 %[[VAL_27]], %[[VAL_65]]
// CHECK:   %[[VAL_70:.*]] = select i1 %[[VAL_68]], i32 %[[VAL_69]], i32 %[[VAL_36]]
// CHECK:   br label %[[VAL_71:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_72:.*]] = phi i32 [ 0, %[[VAL_66]] ], [ %[[VAL_73:.*]], %[[VAL_74:.*]] ]
// CHECK:   br label %[[VAL_75:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_76:.*]] = icmp ult i32 %[[VAL_72]], %[[VAL_70]]
// CHECK:   br i1 %[[VAL_76]], label %[[VAL_77:.*]], label %[[VAL_78:.*]]
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_50]]
// CHECK: omp_dispatch.inc:
// CHECK:   %[[VAL_49]] = add nuw i32 %[[VAL_48]], 1
// CHECK:   br label %[[VAL_47]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_79:.*]] = add i32 %[[VAL_72]], %[[VAL_65]]
// CHECK:   %[[VAL_80:.*]] = mul i32 %[[VAL_79]], 1
// CHECK:   %[[VAL_81:.*]] = add i32 %[[VAL_80]], 0
// CHECK:   br label %[[VAL_82:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_83:.*]]
// CHECK: omp.region.cont9:
// CHECK:   br label %[[VAL_74]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_73]] = add nuw i32 %[[VAL_72]], 1
// CHECK:   br label %[[VAL_71]], !llvm.loop !1
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @distribute_wsloop_dist_schedule_chunked_i64(
// CHECK:   %[[VAL_0:.*]] = zext i32 %[[VAL_1:.*]] to i64
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_push_num_teams_51(ptr @1, i32 %[[VAL_3]], i32 %[[VAL_4:.*]], i32 %[[VAL_4]], i32 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 1, ptr @distribute_wsloop_dist_schedule_chunked_i64..omp_par.2, i64 %[[VAL_0]])
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: teams.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_dist_schedule_chunked_i64..omp_par.2(
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
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @distribute_wsloop_dist_schedule_chunked_i64..omp_par, i64 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_dist_schedule_chunked_i64..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_6:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_7:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.after_alloca2:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region4:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: distribute.alloca:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.after_alloca7:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.after_alloca5:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_17:.*]] = select i1 false, i64 %[[VAL_18:.*]], i64 0
// CHECK:   %[[VAL_19:.*]] = select i1 false, i64 0, i64 %[[VAL_18]]
// CHECK:   %[[VAL_20:.*]] = sub nsw i64 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_21:.*]] = icmp sle i64 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_22:.*]] = sub i64 %[[VAL_20]], 1
// CHECK:   %[[VAL_23:.*]] = udiv i64 %[[VAL_22]], 1
// CHECK:   %[[VAL_24:.*]] = add i64 %[[VAL_23]], 1
// CHECK:   %[[VAL_25:.*]] = icmp ule i64 %[[VAL_20]], 1
// CHECK:   %[[VAL_26:.*]] = select i1 %[[VAL_25]], i64 1, i64 %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = select i1 %[[VAL_21]], i64 0, i64 %[[VAL_26]]
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_29:.*]] = sub i64 %[[VAL_27]], 1
// CHECK:   %[[VAL_30:.*]] = icmp eq i64 %[[VAL_27]], 0
// CHECK:   %[[VAL_31:.*]] = select i1 %[[VAL_30]], i64 0, i64 %[[VAL_29]]
// CHECK:   store i64 %[[VAL_31]], ptr %[[VAL_6]], align 4
// CHECK:   store i64 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_32:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_8u(ptr @1, i32 %[[VAL_32]], i32 34, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i64 1, i64 1)
// CHECK:   call void @__kmpc_for_static_init_8u(ptr @1, i32 %[[VAL_32]], i32 91, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i64 1, i64 1024)
// CHECK:   %[[VAL_33:.*]] = load i64, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_34:.*]] = load i64, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], 1
// CHECK:   %[[VAL_36:.*]] = sub i64 %[[VAL_35]], %[[VAL_33]]
// CHECK:   %[[VAL_37:.*]] = load i64, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_38:.*]] = sub nuw i64 %[[VAL_27]], %[[VAL_33]]
// CHECK:   %[[VAL_39:.*]] = icmp ule i64 %[[VAL_27]], %[[VAL_33]]
// CHECK:   %[[VAL_40:.*]] = sub i64 %[[VAL_38]], 1
// CHECK:   %[[VAL_41:.*]] = udiv i64 %[[VAL_40]], %[[VAL_37]]
// CHECK:   %[[VAL_42:.*]] = add i64 %[[VAL_41]], 1
// CHECK:   %[[VAL_43:.*]] = icmp ule i64 %[[VAL_38]], %[[VAL_37]]
// CHECK:   %[[VAL_44:.*]] = select i1 %[[VAL_43]], i64 1, i64 %[[VAL_42]]
// CHECK:   %[[VAL_45:.*]] = select i1 %[[VAL_39]], i64 0, i64 %[[VAL_44]]
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: omp_dispatch.preheader:
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp_dispatch.header:
// CHECK:   %[[VAL_48:.*]] = phi i64 [ 0, %[[VAL_46]] ], [ %[[VAL_49:.*]], %[[VAL_50:.*]] ]
// CHECK:   br label %[[VAL_51:.*]]
// CHECK: omp_dispatch.cond:
// CHECK:   %[[VAL_52:.*]] = icmp ult i64 %[[VAL_48]], %[[VAL_45]]
// CHECK:   br i1 %[[VAL_52]], label %[[VAL_53:.*]], label %[[VAL_54:.*]]
// CHECK: omp_dispatch.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_32]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_32]])
// CHECK:   br label %[[VAL_55:.*]]
// CHECK: omp_dispatch.after:
// CHECK:   br label %[[VAL_56:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_57:.*]]
// CHECK: omp.region.cont8:
// CHECK:   br label %[[VAL_58:.*]]
// CHECK: omp.region.cont6:
// CHECK:   br label %[[VAL_59:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_60:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_61:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_62:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_63:.*]]
// CHECK: omp_dispatch.body:
// CHECK:   %[[VAL_64:.*]] = mul i64 %[[VAL_48]], %[[VAL_37]]
// CHECK:   %[[VAL_65:.*]] = add i64 %[[VAL_64]], %[[VAL_33]]
// CHECK:   br label %[[VAL_66:.*]]
// CHECK: omp_loop.preheader11:
// CHECK:   %[[VAL_67:.*]] = add i64 %[[VAL_65]], %[[VAL_36]]
// CHECK:   %[[VAL_68:.*]] = icmp uge i64 %[[VAL_67]], %[[VAL_27]]
// CHECK:   %[[VAL_69:.*]] = sub i64 %[[VAL_27]], %[[VAL_65]]
// CHECK:   %[[VAL_70:.*]] = select i1 %[[VAL_68]], i64 %[[VAL_69]], i64 %[[VAL_36]]
// CHECK:   br label %[[VAL_71:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_72:.*]] = phi i64 [ 0, %[[VAL_66]] ], [ %[[VAL_73:.*]], %[[VAL_74:.*]] ]
// CHECK:   br label %[[VAL_75:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_76:.*]] = icmp ult i64 %[[VAL_72]], %[[VAL_70]]
// CHECK:   br i1 %[[VAL_76]], label %[[VAL_77:.*]], label %[[VAL_78:.*]]
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_50]]
// CHECK: omp_dispatch.inc:
// CHECK:   %[[VAL_49]] = add nuw i64 %[[VAL_48]], 1
// CHECK:   br label %[[VAL_47]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_79:.*]] = add i64 %[[VAL_72]], %[[VAL_65]]
// CHECK:   %[[VAL_80:.*]] = mul i64 %[[VAL_79]], 1
// CHECK:   %[[VAL_81:.*]] = add i64 %[[VAL_80]], 0
// CHECK:   br label %[[VAL_82:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_83:.*]]
// CHECK: omp.region.cont9:
// CHECK:   br label %[[VAL_74]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_73]] = add nuw i64 %[[VAL_72]], 1
// CHECK:   br label %[[VAL_71]], !llvm.loop !4
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @distribute_wsloop_schedule_chunked(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_push_num_teams_51(ptr @1, i32 %[[VAL_1]], i32 %[[VAL_2:.*]], i32 %[[VAL_2]], i32 %[[VAL_3:.*]])
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 1, ptr @distribute_wsloop_schedule_chunked..omp_par.1, i32 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: teams.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_schedule_chunked..omp_par.1(
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
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @distribute_wsloop_schedule_chunked..omp_par, i32 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_schedule_chunked..omp_par(
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
// CHECK: omp.region.after_alloca2:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region4:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: distribute.alloca:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.after_alloca7:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.after_alloca5:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_17:.*]] = select i1 false, i32 %[[VAL_18:.*]], i32 0
// CHECK:   %[[VAL_19:.*]] = select i1 false, i32 0, i32 %[[VAL_18]]
// CHECK:   %[[VAL_20:.*]] = sub nsw i32 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_21:.*]] = icmp sle i32 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_22:.*]] = sub i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_23:.*]] = udiv i32 %[[VAL_22]], 1
// CHECK:   %[[VAL_24:.*]] = add i32 %[[VAL_23]], 1
// CHECK:   %[[VAL_25:.*]] = icmp ule i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_26:.*]] = select i1 %[[VAL_25]], i32 1, i32 %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = select i1 %[[VAL_21]], i32 0, i32 %[[VAL_26]]
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_29:.*]] = sub i32 %[[VAL_27]], 1
// CHECK:   %[[VAL_30:.*]] = icmp eq i32 %[[VAL_27]], 0
// CHECK:   %[[VAL_31:.*]] = select i1 %[[VAL_30]], i32 0, i32 %[[VAL_29]]
// CHECK:   store i32 %[[VAL_31]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_32:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_32]], i32 33, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 64)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_32]], i32 92, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   %[[VAL_33:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_34:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_35:.*]] = add i32 %[[VAL_34]], 1
// CHECK:   %[[VAL_36:.*]] = sub i32 %[[VAL_35]], %[[VAL_33]]
// CHECK:   %[[VAL_37:.*]] = load i32, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_38:.*]] = sub nuw i32 %[[VAL_27]], %[[VAL_33]]
// CHECK:   %[[VAL_39:.*]] = icmp ule i32 %[[VAL_27]], %[[VAL_33]]
// CHECK:   %[[VAL_40:.*]] = sub i32 %[[VAL_38]], 1
// CHECK:   %[[VAL_41:.*]] = udiv i32 %[[VAL_40]], %[[VAL_37]]
// CHECK:   %[[VAL_42:.*]] = add i32 %[[VAL_41]], 1
// CHECK:   %[[VAL_43:.*]] = icmp ule i32 %[[VAL_38]], %[[VAL_37]]
// CHECK:   %[[VAL_44:.*]] = select i1 %[[VAL_43]], i32 1, i32 %[[VAL_42]]
// CHECK:   %[[VAL_45:.*]] = select i1 %[[VAL_39]], i32 0, i32 %[[VAL_44]]
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: omp_dispatch.preheader:
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp_dispatch.header:
// CHECK:   %[[VAL_48:.*]] = phi i32 [ 0, %[[VAL_46]] ], [ %[[VAL_49:.*]], %[[VAL_50:.*]] ]
// CHECK:   br label %[[VAL_51:.*]]
// CHECK: omp_dispatch.cond:
// CHECK:   %[[VAL_52:.*]] = icmp ult i32 %[[VAL_48]], %[[VAL_45]]
// CHECK:   br i1 %[[VAL_52]], label %[[VAL_53:.*]], label %[[VAL_54:.*]]
// CHECK: omp_dispatch.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_32]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_32]])
// CHECK:   br label %[[VAL_55:.*]]
// CHECK: omp_dispatch.after:
// CHECK:   br label %[[VAL_56:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_57:.*]]
// CHECK: omp.region.cont8:
// CHECK:   br label %[[VAL_58:.*]]
// CHECK: omp.region.cont6:
// CHECK:   br label %[[VAL_59:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_60:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_61:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_62:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_63:.*]]
// CHECK: omp_dispatch.body:
// CHECK:   %[[VAL_64:.*]] = mul i32 %[[VAL_48]], %[[VAL_37]]
// CHECK:   %[[VAL_65:.*]] = add i32 %[[VAL_64]], %[[VAL_33]]
// CHECK:   br label %[[VAL_66:.*]]
// CHECK: omp_loop.preheader11:
// CHECK:   %[[VAL_67:.*]] = add i32 %[[VAL_65]], %[[VAL_36]]
// CHECK:   %[[VAL_68:.*]] = icmp uge i32 %[[VAL_67]], %[[VAL_27]]
// CHECK:   %[[VAL_69:.*]] = sub i32 %[[VAL_27]], %[[VAL_65]]
// CHECK:   %[[VAL_70:.*]] = select i1 %[[VAL_68]], i32 %[[VAL_69]], i32 %[[VAL_36]]
// CHECK:   br label %[[VAL_71:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_72:.*]] = phi i32 [ 0, %[[VAL_66]] ], [ %[[VAL_73:.*]], %[[VAL_74:.*]] ]
// CHECK:   br label %[[VAL_75:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_76:.*]] = icmp ult i32 %[[VAL_72]], %[[VAL_70]]
// CHECK:   br i1 %[[VAL_76]], label %[[VAL_77:.*]], label %[[VAL_78:.*]]
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_50]]
// CHECK: omp_dispatch.inc:
// CHECK:   %[[VAL_49]] = add nuw i32 %[[VAL_48]], 1
// CHECK:   br label %[[VAL_47]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_79:.*]] = add i32 %[[VAL_72]], %[[VAL_65]]
// CHECK:   %[[VAL_80:.*]] = mul i32 %[[VAL_79]], 1
// CHECK:   %[[VAL_81:.*]] = add i32 %[[VAL_80]], 0
// CHECK:   br label %[[VAL_82:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_83:.*]]
// CHECK: omp.region.cont9:
// CHECK:   br label %[[VAL_74]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_73]] = add nuw i32 %[[VAL_72]], 1
// CHECK:   br label %[[VAL_71]], !llvm.loop !1
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @distribute_wsloop_schedule_chunked_i64(
// CHECK:   %[[VAL_0:.*]] = zext i32 %[[VAL_1:.*]] to i64
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_push_num_teams_51(ptr @1, i32 %[[VAL_3]], i32 %[[VAL_4:.*]], i32 %[[VAL_4]], i32 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 1, ptr @distribute_wsloop_schedule_chunked_i64..omp_par.2, i64 %[[VAL_0]])
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: teams.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_schedule_chunked_i64..omp_par.2(
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
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @distribute_wsloop_schedule_chunked_i64..omp_par, i64 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_schedule_chunked_i64..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_6:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_7:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.after_alloca2:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region4:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: distribute.alloca:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.after_alloca7:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.after_alloca5:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_17:.*]] = select i1 false, i64 %[[VAL_18:.*]], i64 0
// CHECK:   %[[VAL_19:.*]] = select i1 false, i64 0, i64 %[[VAL_18]]
// CHECK:   %[[VAL_20:.*]] = sub nsw i64 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_21:.*]] = icmp sle i64 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_22:.*]] = sub i64 %[[VAL_20]], 1
// CHECK:   %[[VAL_23:.*]] = udiv i64 %[[VAL_22]], 1
// CHECK:   %[[VAL_24:.*]] = add i64 %[[VAL_23]], 1
// CHECK:   %[[VAL_25:.*]] = icmp ule i64 %[[VAL_20]], 1
// CHECK:   %[[VAL_26:.*]] = select i1 %[[VAL_25]], i64 1, i64 %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = select i1 %[[VAL_21]], i64 0, i64 %[[VAL_26]]
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_29:.*]] = sub i64 %[[VAL_27]], 1
// CHECK:   %[[VAL_30:.*]] = icmp eq i64 %[[VAL_27]], 0
// CHECK:   %[[VAL_31:.*]] = select i1 %[[VAL_30]], i64 0, i64 %[[VAL_29]]
// CHECK:   store i64 %[[VAL_31]], ptr %[[VAL_6]], align 4
// CHECK:   store i64 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_32:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_8u(ptr @1, i32 %[[VAL_32]], i32 33, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i64 1, i64 64)
// CHECK:   call void @__kmpc_for_static_init_8u(ptr @1, i32 %[[VAL_32]], i32 92, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i64 1, i64 1)
// CHECK:   %[[VAL_33:.*]] = load i64, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_34:.*]] = load i64, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_35:.*]] = add i64 %[[VAL_34]], 1
// CHECK:   %[[VAL_36:.*]] = sub i64 %[[VAL_35]], %[[VAL_33]]
// CHECK:   %[[VAL_37:.*]] = load i64, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_38:.*]] = sub nuw i64 %[[VAL_27]], %[[VAL_33]]
// CHECK:   %[[VAL_39:.*]] = icmp ule i64 %[[VAL_27]], %[[VAL_33]]
// CHECK:   %[[VAL_40:.*]] = sub i64 %[[VAL_38]], 1
// CHECK:   %[[VAL_41:.*]] = udiv i64 %[[VAL_40]], %[[VAL_37]]
// CHECK:   %[[VAL_42:.*]] = add i64 %[[VAL_41]], 1
// CHECK:   %[[VAL_43:.*]] = icmp ule i64 %[[VAL_38]], %[[VAL_37]]
// CHECK:   %[[VAL_44:.*]] = select i1 %[[VAL_43]], i64 1, i64 %[[VAL_42]]
// CHECK:   %[[VAL_45:.*]] = select i1 %[[VAL_39]], i64 0, i64 %[[VAL_44]]
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: omp_dispatch.preheader:
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp_dispatch.header:
// CHECK:   %[[VAL_48:.*]] = phi i64 [ 0, %[[VAL_46]] ], [ %[[VAL_49:.*]], %[[VAL_50:.*]] ]
// CHECK:   br label %[[VAL_51:.*]]
// CHECK: omp_dispatch.cond:
// CHECK:   %[[VAL_52:.*]] = icmp ult i64 %[[VAL_48]], %[[VAL_45]]
// CHECK:   br i1 %[[VAL_52]], label %[[VAL_53:.*]], label %[[VAL_54:.*]]
// CHECK: omp_dispatch.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_32]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_32]])
// CHECK:   br label %[[VAL_55:.*]]
// CHECK: omp_dispatch.after:
// CHECK:   br label %[[VAL_56:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_57:.*]]
// CHECK: omp.region.cont8:
// CHECK:   br label %[[VAL_58:.*]]
// CHECK: omp.region.cont6:
// CHECK:   br label %[[VAL_59:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_60:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_61:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_62:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_63:.*]]
// CHECK: omp_dispatch.body:
// CHECK:   %[[VAL_64:.*]] = mul i64 %[[VAL_48]], %[[VAL_37]]
// CHECK:   %[[VAL_65:.*]] = add i64 %[[VAL_64]], %[[VAL_33]]
// CHECK:   br label %[[VAL_66:.*]]
// CHECK: omp_loop.preheader11:
// CHECK:   %[[VAL_67:.*]] = add i64 %[[VAL_65]], %[[VAL_36]]
// CHECK:   %[[VAL_68:.*]] = icmp uge i64 %[[VAL_67]], %[[VAL_27]]
// CHECK:   %[[VAL_69:.*]] = sub i64 %[[VAL_27]], %[[VAL_65]]
// CHECK:   %[[VAL_70:.*]] = select i1 %[[VAL_68]], i64 %[[VAL_69]], i64 %[[VAL_36]]
// CHECK:   br label %[[VAL_71:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_72:.*]] = phi i64 [ 0, %[[VAL_66]] ], [ %[[VAL_73:.*]], %[[VAL_74:.*]] ]
// CHECK:   br label %[[VAL_75:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_76:.*]] = icmp ult i64 %[[VAL_72]], %[[VAL_70]]
// CHECK:   br i1 %[[VAL_76]], label %[[VAL_77:.*]], label %[[VAL_78:.*]]
// CHECK: omp_loop.exit:
// CHECK:   br label %[[VAL_50]]
// CHECK: omp_dispatch.inc:
// CHECK:   %[[VAL_49]] = add nuw i64 %[[VAL_48]], 1
// CHECK:   br label %[[VAL_47]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_79:.*]] = add i64 %[[VAL_72]], %[[VAL_65]]
// CHECK:   %[[VAL_80:.*]] = mul i64 %[[VAL_79]], 1
// CHECK:   %[[VAL_81:.*]] = add i64 %[[VAL_80]], 0
// CHECK:   br label %[[VAL_82:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_83:.*]]
// CHECK: omp.region.cont9:
// CHECK:   br label %[[VAL_74]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_73]] = add nuw i64 %[[VAL_72]], 1
// CHECK:   br label %[[VAL_71]], !llvm.loop !4
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @distribute_wsloop_no_chunks(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_push_num_teams_51(ptr @1, i32 %[[VAL_1]], i32 %[[VAL_2:.*]], i32 %[[VAL_2]], i32 %[[VAL_3:.*]])
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 1, ptr @distribute_wsloop_no_chunks..omp_par.1, i32 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: teams.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_no_chunks..omp_par.1(
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
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @distribute_wsloop_no_chunks..omp_par, i32 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_no_chunks..omp_par(
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
// CHECK: omp.region.after_alloca2:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region4:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: distribute.alloca:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.after_alloca7:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.after_alloca5:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_17:.*]] = select i1 false, i32 %[[VAL_18:.*]], i32 0
// CHECK:   %[[VAL_19:.*]] = select i1 false, i32 0, i32 %[[VAL_18]]
// CHECK:   %[[VAL_20:.*]] = sub nsw i32 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_21:.*]] = icmp sle i32 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_22:.*]] = sub i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_23:.*]] = udiv i32 %[[VAL_22]], 1
// CHECK:   %[[VAL_24:.*]] = add i32 %[[VAL_23]], 1
// CHECK:   %[[VAL_25:.*]] = icmp ule i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_26:.*]] = select i1 %[[VAL_25]], i32 1, i32 %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = select i1 %[[VAL_21]], i32 0, i32 %[[VAL_26]]
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_29:.*]] = sub i32 %[[VAL_27]], 1
// CHECK:   store i32 %[[VAL_29]], ptr %[[VAL_6]], align 4
// CHECK:   store i32 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_30:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_30]], i32 34, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_30]], i32 92, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i32 1, i32 1)
// CHECK:   %[[VAL_31:.*]] = load i32, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_32:.*]] = load i32, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_33:.*]] = sub i32 %[[VAL_32]], %[[VAL_31]]
// CHECK:   %[[VAL_34:.*]] = add i32 %[[VAL_33]], 1
// CHECK:   br label %[[VAL_35:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_36:.*]] = phi i32 [ 0, %[[VAL_28]] ], [ %[[VAL_37:.*]], %[[VAL_38:.*]] ]
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_40:.*]] = icmp ult i32 %[[VAL_36]], %[[VAL_34]]
// CHECK:   br i1 %[[VAL_40]], label %[[VAL_41:.*]], label %[[VAL_42:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_30]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_30]])
// CHECK:   br label %[[VAL_43:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.region.cont8:
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: omp.region.cont6:
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_49:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_50:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_51:.*]] = add i32 %[[VAL_36]], %[[VAL_31]]
// CHECK:   %[[VAL_52:.*]] = mul i32 %[[VAL_51]], 1
// CHECK:   %[[VAL_53:.*]] = add i32 %[[VAL_52]], 0
// CHECK:   br label %[[VAL_54:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_55:.*]]
// CHECK: omp.region.cont9:
// CHECK:   br label %[[VAL_38]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_37]] = add nuw i32 %[[VAL_36]], 1
// CHECK:   br label %[[VAL_35]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @distribute_wsloop_no_chunks_i64(
// CHECK:   %[[VAL_0:.*]] = zext i32 %[[VAL_1:.*]] to i64
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_push_num_teams_51(ptr @1, i32 %[[VAL_3]], i32 %[[VAL_4:.*]], i32 %[[VAL_4]], i32 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: codeRepl:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_teams(ptr @1, i32 1, ptr @distribute_wsloop_no_chunks_i64..omp_par.2, i64 %[[VAL_0]])
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: teams.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_no_chunks_i64..omp_par.2(
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
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @distribute_wsloop_no_chunks_i64..omp_par, i64 %[[VAL_5:.*]])
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.exit:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: teams.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @distribute_wsloop_no_chunks_i64..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_4:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_5:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_6:.*]] = alloca i64, align 8
// CHECK:   %[[VAL_7:.*]] = alloca i64, align 8
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.after_alloca2:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_10:.*]]
// CHECK: omp.par.region4:
// CHECK:   br label %[[VAL_11:.*]]
// CHECK: distribute.alloca:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.after_alloca7:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.after_alloca5:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: distribute.body:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.distribute.region:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_17:.*]] = select i1 false, i64 %[[VAL_18:.*]], i64 0
// CHECK:   %[[VAL_19:.*]] = select i1 false, i64 0, i64 %[[VAL_18]]
// CHECK:   %[[VAL_20:.*]] = sub nsw i64 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_21:.*]] = icmp sle i64 %[[VAL_19]], %[[VAL_17]]
// CHECK:   %[[VAL_22:.*]] = sub i64 %[[VAL_20]], 1
// CHECK:   %[[VAL_23:.*]] = udiv i64 %[[VAL_22]], 1
// CHECK:   %[[VAL_24:.*]] = add i64 %[[VAL_23]], 1
// CHECK:   %[[VAL_25:.*]] = icmp ule i64 %[[VAL_20]], 1
// CHECK:   %[[VAL_26:.*]] = select i1 %[[VAL_25]], i64 1, i64 %[[VAL_24]]
// CHECK:   %[[VAL_27:.*]] = select i1 %[[VAL_21]], i64 0, i64 %[[VAL_26]]
// CHECK:   br label %[[VAL_28:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i64 0, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_29:.*]] = sub i64 %[[VAL_27]], 1
// CHECK:   store i64 %[[VAL_29]], ptr %[[VAL_6]], align 4
// CHECK:   store i64 1, ptr %[[VAL_7]], align 4
// CHECK:   %[[VAL_30:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_8u(ptr @1, i32 %[[VAL_30]], i32 34, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i64 1, i64 1)
// CHECK:   call void @__kmpc_for_static_init_8u(ptr @1, i32 %[[VAL_30]], i32 92, ptr %[[VAL_4]], ptr %[[VAL_5]], ptr %[[VAL_6]], ptr %[[VAL_7]], i64 1, i64 1)
// CHECK:   %[[VAL_31:.*]] = load i64, ptr %[[VAL_5]], align 4
// CHECK:   %[[VAL_32:.*]] = load i64, ptr %[[VAL_6]], align 4
// CHECK:   %[[VAL_33:.*]] = sub i64 %[[VAL_32]], %[[VAL_31]]
// CHECK:   %[[VAL_34:.*]] = add i64 %[[VAL_33]], 1
// CHECK:   br label %[[VAL_35:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_36:.*]] = phi i64 [ 0, %[[VAL_28]] ], [ %[[VAL_37:.*]], %[[VAL_38:.*]] ]
// CHECK:   br label %[[VAL_39:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_40:.*]] = icmp ult i64 %[[VAL_36]], %[[VAL_34]]
// CHECK:   br i1 %[[VAL_40]], label %[[VAL_41:.*]], label %[[VAL_42:.*]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_30]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_30]])
// CHECK:   br label %[[VAL_43:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_44:.*]]
// CHECK: omp.region.cont8:
// CHECK:   br label %[[VAL_45:.*]]
// CHECK: omp.region.cont6:
// CHECK:   br label %[[VAL_46:.*]]
// CHECK: distribute.exit:
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp.region.cont3:
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_49:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_50:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_51:.*]] = add i64 %[[VAL_36]], %[[VAL_31]]
// CHECK:   %[[VAL_52:.*]] = mul i64 %[[VAL_51]], 1
// CHECK:   %[[VAL_53:.*]] = add i64 %[[VAL_52]], 0
// CHECK:   br label %[[VAL_54:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   br label %[[VAL_55:.*]]
// CHECK: omp.region.cont9:
// CHECK:   br label %[[VAL_38]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_37]] = add nuw i64 %[[VAL_36]], 1
// CHECK:   br label %[[VAL_35]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
