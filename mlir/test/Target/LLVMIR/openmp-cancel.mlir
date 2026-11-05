// RUN: mlir-translate --mlir-to-llvmir %s | FileCheck %s

llvm.func @cancel_parallel() {
  omp.parallel {
    omp.cancel cancellation_construct_type(parallel)
    omp.terminator
  }
  llvm.return
}

llvm.func @cancel_parallel_if(%arg0 : i1) {
  omp.parallel {
    omp.cancel cancellation_construct_type(parallel) if(%arg0)
    omp.terminator
  }
  llvm.return
}

llvm.func @cancel_sections_if(%cond : i1) {
  omp.sections {
    omp.section {
      omp.cancel cancellation_construct_type(sections) if(%cond)
      omp.terminator
    }
    omp.terminator
  }
  llvm.return
}

llvm.func @cancel_wsloop_if(%lb : i32, %ub : i32, %step : i32, %cond : i1) {
  omp.wsloop {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.cancel cancellation_construct_type(loop) if(%cond)
      omp.yield
    }
  }
  llvm.return
}

omp.private {type = firstprivate} @i32_priv : i32 copy {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %0 = llvm.load %arg0 : !llvm.ptr -> i32
  llvm.store %0, %arg1 : i32, !llvm.ptr
  omp.yield(%arg1 : !llvm.ptr)
}

llvm.func @do_something(!llvm.ptr)

llvm.func @cancel_taskgroup(%arg0: !llvm.ptr) {
  omp.taskgroup {
// Using firstprivate clause so we have some end of task cleanup to branch to
// after the cancellation.
    omp.task private(@i32_priv %arg0 -> %arg1 : !llvm.ptr) {
      omp.cancel cancellation_construct_type(taskgroup)
      llvm.call @do_something(%arg1) : (!llvm.ptr) -> ()
      omp.terminator
    }
    omp.terminator
  }
  llvm.return
}
// Both cancellation and normal paths reach the end-of-task cleanup:

// CHECK: @0 = private unnamed_addr constant [23 x i8] c";unknown;unknown;0;0;;\00", align 1
// CHECK: @1 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 2, i32 0, i32 22, ptr @0 }, align 8
// CHECK: @2 = private unnamed_addr constant %struct.ident_t {{[{]}} i32 0, i32 66, i32 0, i32 22, ptr @0 }, align 8
// CHECK-LABEL: define void @cancel_parallel(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 0, ptr @cancel_parallel..omp_par)
// CHECK:   br label %[[VAL_3:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @cancel_parallel..omp_par(
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
// CHECK:   %[[VAL_8:.*]] = call i32 @__kmpc_cancel(ptr @1, i32 %[[VAL_7]], i32 1)
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
// CHECK-LABEL: define void @cancel_parallel_if(
// CHECK:   br label %[[VAL_0:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_1:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_2:.*]]
// CHECK: omp_parallel:
// CHECK:   call void (ptr, i32, ptr, ...) @__kmpc_fork_call(ptr @1, i32 1, ptr @cancel_parallel_if..omp_par, i1 %[[VAL_3:.*]])
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.par.exit:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define internal void @cancel_parallel_if..omp_par(
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
// CHECK:   br i1 %[[VAL_7:.*]], label %[[VAL_8:.*]], label %[[VAL_9:.*]]
// CHECK: 2:
// CHECK:   %[[VAL_10:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_11:.*]] = call i32 @__kmpc_cancellationpoint(ptr @1, i32 %[[VAL_10]], i32 1)
// CHECK:   %[[VAL_12:.*]] = icmp eq i32 %[[VAL_11]], 0
// CHECK:   br i1 %[[VAL_12]], label %[[VAL_13:.*]], label %[[VAL_14:.*]]
// CHECK: .cncl:
// CHECK:   br label %[[VAL_15:.*]]
// CHECK: .fini:
// CHECK:   %[[VAL_16:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_17:.*]] = call i32 @__kmpc_cancel_barrier(ptr @2, i32 %[[VAL_16]])
// CHECK:   br label %[[VAL_18:.*]]
// CHECK: .split:
// CHECK:   br label %[[VAL_19:.*]]
// CHECK: 6:
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: omp.par.pre_finalize:
// CHECK:   br label %[[VAL_15]]
// CHECK: 7:
// CHECK:   %[[VAL_22:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_23:.*]] = call i32 @__kmpc_cancel(ptr @1, i32 %[[VAL_22]], i32 1)
// CHECK:   %[[VAL_24:.*]] = icmp eq i32 %[[VAL_23]], 0
// CHECK:   br i1 %[[VAL_24]], label %[[VAL_25:.*]], label %[[VAL_26:.*]]
// CHECK: .cncl6:
// CHECK:   br label %[[VAL_15]]
// CHECK: .split5:
// CHECK:   br label %[[VAL_19]]
// CHECK: omp.par.exit.exitStub:
// CHECK:   ret void
// CHECK: }
// CHECK-LABEL: define void @cancel_sections_if(
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
// CHECK:   br i1 %[[VAL_26:.*]], label %[[VAL_27:.*]], label %[[VAL_28:.*]]
// CHECK: 9:
// CHECK:   %[[VAL_29:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_30:.*]] = call i32 @__kmpc_cancel(ptr @1, i32 %[[VAL_29]], i32 3)
// CHECK:   %[[VAL_31:.*]] = icmp eq i32 %[[VAL_30]], 0
// CHECK:   br i1 %[[VAL_31]], label %[[VAL_32:.*]], label %[[VAL_33:.*]]
// CHECK: .split2:
// CHECK:   br label %[[VAL_34:.*]]
// CHECK: 12:
// CHECK:   %[[VAL_35:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_36:.*]] = call i32 @__kmpc_cancellationpoint(ptr @1, i32 %[[VAL_35]], i32 3)
// CHECK:   %[[VAL_37:.*]] = icmp eq i32 %[[VAL_36]], 0
// CHECK:   br i1 %[[VAL_37]], label %[[VAL_38:.*]], label %[[VAL_39:.*]]
// CHECK: .split:
// CHECK:   br label %[[VAL_34]]
// CHECK: 15:
// CHECK:   br label %[[VAL_40:.*]]
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
// CHECK:   br label %[[VAL_41:.*]]
// CHECK: omp_section_loop.after:
// CHECK:   ret void
// CHECK: .cncl:
// CHECK:   br label %[[VAL_19]]
// CHECK: .cncl3:
// CHECK:   br label %[[VAL_19]]
// CHECK: }
// CHECK-LABEL: define void @cancel_wsloop_if(
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
// CHECK:   br i1 %[[VAL_42:.*]], label %[[VAL_43:.*]], label %[[VAL_44:.*]]
// CHECK: 25:
// CHECK:   %[[VAL_45:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_46:.*]] = call i32 @__kmpc_cancel(ptr @1, i32 %[[VAL_45]], i32 2)
// CHECK:   %[[VAL_47:.*]] = icmp eq i32 %[[VAL_46]], 0
// CHECK:   br i1 %[[VAL_47]], label %[[VAL_48:.*]], label %[[VAL_49:.*]]
// CHECK: .split3:
// CHECK:   br label %[[VAL_50:.*]]
// CHECK: 28:
// CHECK:   %[[VAL_51:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_52:.*]] = call i32 @__kmpc_cancellationpoint(ptr @1, i32 %[[VAL_51]], i32 2)
// CHECK:   %[[VAL_53:.*]] = icmp eq i32 %[[VAL_52]], 0
// CHECK:   br i1 %[[VAL_53]], label %[[VAL_54:.*]], label %[[VAL_55:.*]]
// CHECK: .split:
// CHECK:   br label %[[VAL_50]]
// CHECK: 31:
// CHECK:   br label %[[VAL_56:.*]]
// CHECK: omp.region.cont1:
// CHECK:   br label %[[VAL_33]]
// CHECK: omp_loop.inc:
// CHECK:   %[[VAL_32]] = add nuw i32 %[[VAL_31]], 1
// CHECK:   br label %[[VAL_30]]
// CHECK: omp_loop.exit:
// CHECK:   call void @__kmpc_for_static_fini(ptr @1, i32 %[[VAL_25]])
// CHECK:   call void @__kmpc_barrier(ptr @2, i32 %[[VAL_25]])
// CHECK:   br label %[[VAL_57:.*]]
// CHECK: omp_loop.after:
// CHECK:   br label %[[VAL_58:.*]]
// CHECK: omp.region.cont:
// CHECK:   ret void
// CHECK: .cncl:
// CHECK:   br label %[[VAL_59:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_57]]
// CHECK: .cncl4:
// CHECK:   br label %[[VAL_59]]
// CHECK: }
// CHECK-LABEL: define void @cancel_taskgroup(
// CHECK:   %[[VAL_0:.*]] = alloca {{[{]}} ptr }, align 8
// CHECK:   br label %[[VAL_1:.*]]
// CHECK: entry:
// CHECK:   %[[VAL_2:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   call void @__kmpc_taskgroup(ptr @1, i32 %[[VAL_2]])
// CHECK:   %[[VAL_3:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: omp.taskgroup.region:
// CHECK:   br label %[[VAL_5:.*]]
// CHECK: omp.private.init:
// CHECK:   %[[VAL_6:.*]] = tail call ptr @malloc(i64 4)
// CHECK:   %[[VAL_7:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_6]], i32 0, i32 0
// CHECK:   br label %[[VAL_8:.*]]
// CHECK: omp.private.copy:
// CHECK:   br label %[[VAL_9:.*]]
// CHECK: omp.private.copy2:
// CHECK:   %[[VAL_10:.*]] = load i32, ptr %[[VAL_11:.*]], align 4
// CHECK:   store i32 %[[VAL_10]], ptr %[[VAL_7]], align 4
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: omp.region.cont:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: taskgroup.exit:
// CHECK:   call void @__kmpc_end_taskgroup(ptr @1, i32 %[[VAL_2]])
// CHECK:   ret void
// CHECK: omp.task.start:
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: codeRepl:
// CHECK:   %[[VAL_15:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   store ptr %[[VAL_6]], ptr %[[VAL_15]], align 8
// CHECK:   %[[VAL_16:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_17:.*]] = call ptr @__kmpc_omp_task_alloc(ptr @1, i32 %[[VAL_16]], i32 1, i64 40, i64 8, ptr @cancel_taskgroup..omp_par)
// CHECK:   %[[VAL_18:.*]] = load ptr, ptr %[[VAL_17]], align 8
// CHECK:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 %[[VAL_18]], ptr align 1 %[[VAL_0]], i64 8, i1 false)
// CHECK:   %[[VAL_19:.*]] = call i32 @__kmpc_omp_task(ptr @1, i32 %[[VAL_16]], ptr %[[VAL_17]])
// CHECK:   br label %[[VAL_20:.*]]
// CHECK: task.exit:
// CHECK:   br label %[[VAL_21:.*]]
// CHECK: }
// CHECK-LABEL: define internal void @cancel_taskgroup..omp_par(
// CHECK: task.alloca:
// CHECK:   %[[VAL_0:.*]] = load ptr, ptr %[[VAL_1:.*]], align 8
// CHECK:   %[[VAL_2:.*]] = getelementptr {{[{]}} ptr }, ptr %[[VAL_0]], i32 0, i32 0
// CHECK:   %[[VAL_3:.*]] = load ptr, ptr %[[VAL_2]], align 8, !align !1
// CHECK:   br label %[[VAL_4:.*]]
// CHECK: task.body:
// CHECK:   %[[VAL_5:.*]] = getelementptr {{[{]}} i32 }, ptr %[[VAL_3]], i32 0, i32 0
// CHECK:   br label %[[VAL_6:.*]]
// CHECK: omp.task.region:
// CHECK:   %[[VAL_7:.*]] = call i32 @__kmpc_global_thread_num(ptr @1)
// CHECK:   %[[VAL_8:.*]] = call i32 @__kmpc_cancel(ptr @1, i32 %[[VAL_7]], i32 4)
// CHECK:   %[[VAL_9:.*]] = icmp eq i32 %[[VAL_8]], 0
// CHECK:   br i1 %[[VAL_9]], label %[[VAL_10:.*]], label %[[VAL_11:.*]]
// CHECK: omp.task.region.cncl:
// CHECK:   br label %[[VAL_12:.*]]
// CHECK: .fini:
// CHECK:   br label %[[VAL_13:.*]]
// CHECK: omp.region.cont3:
// CHECK:   tail call void @free(ptr %[[VAL_3]])
// CHECK:   br label %[[VAL_14:.*]]
// CHECK: omp.task.region.split:
// CHECK:   call void @do_something(ptr %[[VAL_5]])
// CHECK:   br label %[[VAL_13]]
// CHECK: task.exit.exitStub:
// CHECK:   ret void
// CHECK: }
