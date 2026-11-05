// RUN: mlir-translate --mlir-to-llvmir %s | FileCheck %s

llvm.func @cancellation_point_parallel() {
  omp.parallel {
    omp.cancellation_point cancellation_construct_type(parallel)
    omp.terminator
  }
  llvm.return
}

llvm.func @cancellation_point_sections() {
  omp.sections {
    omp.section {
      omp.cancellation_point cancellation_construct_type(sections)
      omp.terminator
    }
    omp.terminator
  }
  llvm.return
}

llvm.func @cancellation_point_wsloop(%lb : i32, %ub : i32, %step : i32) {
  omp.wsloop {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.cancellation_point cancellation_construct_type(loop)
      omp.yield
    }
  }
  llvm.return
}


llvm.func @cancellation_point_taskgroup() {
  omp.taskgroup {
    omp.task {
      omp.cancellation_point cancellation_construct_type(taskgroup)
      omp.terminator
    }
    omp.terminator
  }
  llvm.return
}

// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @cancellation_point_parallel(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @cancellation_point_parallel..omp_par)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @cancellation_point_parallel..omp_par(
// CHECK: omp.par.entry:
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = load i32, ptr %[[VAL_2:.*]], align 4
// CHECK:   store i32 %[[VAL_1]], ptr %[[VAL_0]], align 4
// CHECK:   %[[VAL_3:.*]] = load i32, ptr %[[VAL_0]], align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.par.region:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.par.region1:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_8:.*]] = call i32 @__kmpc_cancellationpoint(ptr @1, i32 %[[VAL_7]], i32 1)
// CHECK:   %[[VAL_9:.*]] = icmp eq i32 %[[VAL_8]], 0
// CHECK:   br i1 %[[VAL_9]], label %[[VAL_10:.*]], label %[[VAL_11:.*]]
// CHECK: omp.par.region1.cncl:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: .fini:
// CHECK:   %[[VAL_13:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_14:.*]] = call i32 @__kmpc_cancel_barrier(ptr @2, i32 %[[VAL_13]])
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: omp.par.region1.split:
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_17:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_12]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @cancellation_point_sections(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp_section_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_1]], align 4
// CHECK:   store i32 0, ptr %[[VAL_2]], align 4
// CHECK:   store i32 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_7]], i32 34, ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], i32 1, i32 1)
// CHECK:   %[[VAL_8:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_9:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_10:.*]] = sub i32 %[[VAL_9]], %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = add i32 %[[VAL_10]], 1
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp_section_loop.header:
// CHECK:   %[[VAL_13:.*]] = phi i32 [ 0, %[[VAL_6]] ], [ %[[VAL_14:.*]], %[[VAL_15:.*]] ]
// CHECK:   br label %[[VAL_16:.*]]
// CHECK: omp_section_loop.cond:
// CHECK:   %[[VAL_17:.*]] = icmp ult i32 %[[VAL_13]], %[[VAL_11]]
// CHECK:   br i1 %[[VAL_17]], label %[[VAL_18:.*]], label %[[VAL_19:.*]]
// CHECK: omp_section_loop.body:
// CHECK:   %[[VAL_20:.*]] = add i32 %[[VAL_13]], %[[VAL_8]]
// CHECK:   %[[VAL_21:.*]] = mul i32 %[[VAL_20]], 1
// CHECK:   %[[VAL_22:.*]] = add i32 %[[VAL_21]], 0
// CHECK:   switch i32 %[[VAL_22]], label %[[VAL_23:.*]] [
// CHECK:   i32 0, label %[[VAL_24:.*]]
// CHECK:   ]
// CHECK: omp_section_loop.body.case:
// CHECK:   br label %[[VAL_25:.*]]
// CHECK: omp.section.region:
// CHECK:   %[[VAL_26:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_27:.*]] = call i32 @__kmpc_cancellationpoint(ptr @1, i32 %[[VAL_26]], i32 3)
// CHECK:   %[[VAL_28:.*]] = icmp eq i32 %[[VAL_27]], 0
// CHECK:   br i1 %[[VAL_28]], label %[[VAL_29:.*]], label %[[VAL_30:.*]]
// CHECK: omp.section.region.split:
// CHECK:   br label %[[VAL_31:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_23]]
// CHECK: omp_section_loop.body.sections.after:
// CHECK:   br label %[[VAL_15]]
// CHECK: omp_section_loop.inc:
// CHECK:   %[[VAL_14]] = add nuw i32 %[[VAL_13]], 1
// CHECK:   br label %[[VAL_12]]
// CHECK: omp_section_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_7]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_7]])
// CHECK:   br label %[[VAL_32:.*]]
// CHECK: omp_section_loop.after:
// CHECK:   ret void
// CHECK: omp.section.region.cncl:
// CHECK:   br label %[[VAL_19]]
// CHECK: }
// CHECK-LABEL: define void @cancellation_point_wsloop(
// CHECK:   %[[VAL_0:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_1:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_2:.*]] = alloca i32, align 4
// CHECK:   %[[VAL_3:.*]] = alloca i32, align 4
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.region.after_alloca:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: entry:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.wsloop.region:
// CHECK:   %[[VAL_7:.*]] = icmp slt i32 %[[VAL_8:.*]], 0
// CHECK:   %[[VAL_9:.*]] = sub i32 0, %[[VAL_8]]
// CHECK:   %[[VAL_10:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_9]], i32 %[[VAL_8]]
// CHECK:   %[[VAL_11:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_12:.*]], i32 %[[VAL_13:.*]]
// CHECK:   %[[VAL_14:.*]] = select i1 %[[VAL_7]], i32 %[[VAL_13]], i32 %[[VAL_12]]
// CHECK:   %[[VAL_15:.*]] = sub nsw i32 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_16:.*]] = icmp sle i32 %[[VAL_14]], %[[VAL_11]]
// CHECK:   %[[VAL_17:.*]] = sub i32 %[[VAL_15]], 1
// CHECK:   %[[VAL_18:.*]] = udiv i32 %[[VAL_17]], %[[VAL_10]]
// CHECK:   %[[VAL_19:.*]] = add i32 %[[VAL_18]], 1
// CHECK:   %[[VAL_20:.*]] = icmp ule i32 %[[VAL_15]], %[[VAL_10]]
// CHECK:   %[[VAL_21:.*]] = select i1 %[[VAL_20]], i32 1, i32 %[[VAL_19]]
// CHECK:   %[[VAL_22:.*]] = select i1 %[[VAL_16]], i32 0, i32 %[[VAL_21]]
// CHECK:   br label %[[VAL_23:.*]]
// CHECK: omp_loop.preheader:
// CHECK:   store i32 0, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_24:.*]] = sub i32 %[[VAL_22]], 1
// CHECK:   store i32 %[[VAL_24]], ptr %[[VAL_2]], align 4
// CHECK:   store i32 1, ptr %[[VAL_3]], align 4
// CHECK:   %[[VAL_25:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_for_static_init_4u(ptr @1, i32 %[[VAL_25]], i32 34, ptr %[[VAL_0]], ptr %[[VAL_1]], ptr %[[VAL_2]], ptr %[[VAL_3]], i32 1, i32 1)
// CHECK:   %[[VAL_26:.*]] = load i32, ptr %[[VAL_1]], align 4
// CHECK:   %[[VAL_27:.*]] = load i32, ptr %[[VAL_2]], align 4
// CHECK:   %[[VAL_28:.*]] = sub i32 %[[VAL_27]], %[[VAL_26]]
// CHECK:   %[[VAL_29:.*]] = add i32 %[[VAL_28]], 1
// CHECK:   br label %[[VAL_30:.*]]
// CHECK: omp_loop.header:
// CHECK:   %[[VAL_31:.*]] = phi i32 [ 0, %[[VAL_23]] ], [ %[[VAL_32:.*]], %[[VAL_33:.*]] ]
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: omp_loop.cond:
// CHECK:   %[[VAL_35:.*]] = icmp ult i32 %[[VAL_31]], %[[VAL_29]]
// CHECK:   br i1 %[[VAL_35]], label %[[VAL_36:.*]], label %[[VAL_37:.*]]
// CHECK: omp_loop.body:
// CHECK:   %[[VAL_38:.*]] = add i32 %[[VAL_31]], %[[VAL_26]]
// CHECK:   %[[VAL_39:.*]] = mul i32 %[[VAL_38]], %[[VAL_8]]
// CHECK:   %[[VAL_40:.*]] = add i32 %[[VAL_39]], %[[VAL_13]]
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp.loop_nest.region:
// CHECK:   %[[VAL_42:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_43:.*]] = call i32 @__kmpc_cancellationpoint(ptr @1, i32 %[[VAL_42]], i32 2)
// CHECK:   %[[VAL_44:.*]] = icmp eq i32 %[[VAL_43]], 0
// CHECK:   br i1 %[[VAL_44]], label %[[VAL_45:.*]], label %[[VAL_46:.*]]
// CHECK: omp.loop_nest.region.split:
// CHECK:   br label %[[VAL_47:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_33]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_32]] = add nuw i32 %[[VAL_31]], 1
// CHECK:   br label %[[VAL_30]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_25]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_25]])
// CHECK:   br label %[[VAL_48:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_49:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: omp.loop_nest.region.cncl:
// CHECK:   br label %[[VAL_50:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_48]]
// CHECK: }
// CHECK-LABEL: define void @cancellation_point_taskgroup(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_taskgroup(ptr @1, i32 %[[VAL_1]])
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.taskgroup.region:
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.private.init:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: taskgroup.exit:
// CHECK:   call void @__kmpc_end_taskgroup(ptr @1, i32 %[[VAL_1]])
// CHECK:   ret void
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_9:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_10:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_9]], i32 1, i64 40, i64 0, ptr @cancellation_point_taskgroup..omp_par)
// CHECK:   %[[VAL_11:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_9]], ptr %[[VAL_10]])
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: task.exit:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: }
// CHECK-LABEL: define internal void @cancellation_point_taskgroup..omp_par(
// CHECK: task.alloca:
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: task.body:
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: omp.task.region:
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_cancellationpoint(ptr @1, i32 %[[VAL_2]], i32 4)
// CHECK:   %[[VAL_4:.*]] = icmp eq i32 %[[VAL_3]], 0
// CHECK:   br i1 %[[VAL_4]], label %[[VAL_5:.*]], label %[[VAL_6:.*]]
// CHECK: omp.task.region.cncl:
// CHECK:   br label %[[VAL_7:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.region.cont2:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.task.region.split:
// CHECK:   br label %[[VAL_8]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
