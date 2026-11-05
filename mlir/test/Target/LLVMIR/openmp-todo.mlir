// RUN: mlir-translate -mlir-to-llvmir -split-input-file -verify-diagnostics %s | FileCheck %s --check-prefix=CHECK-CODEGEN


llvm.func @atomic_hint(%v : !llvm.ptr, %x : !llvm.ptr, %expr : i32) {
  // expected-warning@below {{hint clause discarded}}
  omp.atomic.capture hint(uncontended) {
    omp.atomic.read %x = %v : !llvm.ptr, !llvm.ptr, i32
    omp.atomic.write %v = %expr : !llvm.ptr, i32
  }

  // expected-warning@below {{hint clause discarded}}
  omp.atomic.read %x = %v hint(contended) : !llvm.ptr, !llvm.ptr, i32

  // expected-warning@below {{hint clause discarded}}
  omp.atomic.write %v = %expr hint(nonspeculative) : !llvm.ptr, i32

  // expected-warning@below {{hint clause discarded}}
  omp.atomic.update hint(speculative) %x : !llvm.ptr {
  ^bb0(%arg0: i32):
    %result = llvm.add %arg0, %expr : i32
    omp.yield(%result : i32)
  }

  llvm.return
}

// -----

llvm.func @distribute_allocate(%lb : i32, %ub : i32, %step : i32, %x : !llvm.ptr) {
  // expected-error@below {{not yet implemented: Unhandled clause allocate in omp.distribute operation}}
  // expected-error@below {{LLVM Translation failed for operation: omp.distribute}}
  omp.distribute allocate(%x : !llvm.ptr -> %x : !llvm.ptr) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.yield
    }
  }
  llvm.return
}

// -----

llvm.func @ordered_region_par_level_simd() {
  omp.ordered.region par_level_simd {
    omp.terminator
  }
  llvm.return
}

// CHECK-CODEGEN-LABEL: define void @ordered_region_par_level_simd() {
// CHECK-CODEGEN:       br label %omp.ordered.region
// CHECK-CODEGEN:       omp.ordered.region:
// CHECK-CODEGEN:       br label %omp.region.cont
// CHECK-CODEGEN:       omp.region.cont:
// CHECK-CODEGEN:       br label %omp_region.finalize
// CHECK-CODEGEN:       omp_region.finalize:
// CHECK-CODEGEN:       ret void

// -----

llvm.func @parallel_allocate(%x : !llvm.ptr) {
  // expected-error@below {{omp.parallel allocate clause applied to a non-private variable}}
  // expected-error@below {{LLVM Translation failed for operation: omp.parallel}}
  omp.parallel allocate(%x : !llvm.ptr -> %x : !llvm.ptr) {
    omp.terminator
  }
  llvm.return
}

// -----

llvm.func @sections_allocate(%x : !llvm.ptr) {
  // expected-error@below {{not yet implemented: Unhandled clause allocate in omp.sections operation}}
  // expected-error@below {{LLVM Translation failed for operation: omp.sections}}
  omp.sections allocate(%x : !llvm.ptr -> %x : !llvm.ptr) {
    omp.terminator
  }
  llvm.return
}

// -----

omp.private {type = private} @x.privatizer : i32 init {
^bb0(%mold: !llvm.ptr, %private: !llvm.ptr):
  %c0 = llvm.mlir.constant(0 : i32) : i32
  llvm.store %c0, %private : i32, !llvm.ptr
  omp.yield(%private: !llvm.ptr)
}
llvm.func @sections_private(%x : !llvm.ptr) {
  omp.sections private(@x.privatizer %x -> %arg0 : !llvm.ptr) {
    omp.terminator
  }
  llvm.return
}

// CHECK-CODEGEN-LABEL: define void @sections_private(
// CHECK-CODEGEN:       %[[PRIVATE:.+]] = alloca i32, align 4
// CHECK-CODEGEN:       store i32 0, ptr %[[PRIVATE]], align 4
// CHECK-CODEGEN:       ret void

// -----

llvm.func @single_allocate(%x : !llvm.ptr) {
  // expected-error@below {{not yet implemented: Unhandled clause allocate in omp.single operation}}
  // expected-error@below {{LLVM Translation failed for operation: omp.single}}
  omp.single allocate(%x : !llvm.ptr -> %x : !llvm.ptr) {
    omp.terminator
  }
  llvm.return
}

// -----

omp.private {type = private} @x.privatizer : i32 init {
^bb0(%mold: !llvm.ptr, %private: !llvm.ptr):
  %c0 = llvm.mlir.constant(0 : i32) : i32
  llvm.store %c0, %private : i32, !llvm.ptr
  omp.yield(%private: !llvm.ptr)
}
llvm.func @single_private(%x : !llvm.ptr) {
  // expected-error@below {{not yet implemented: Unhandled clause privatization in omp.single operation}}
  // expected-error@below {{LLVM Translation failed for operation: omp.single}}
  omp.single private(@x.privatizer %x -> %arg0 : !llvm.ptr) {
    omp.terminator
  }
  llvm.return
}

// -----

llvm.func @target_allocate(%x : !llvm.ptr) {
  // expected-error@below {{omp.target allocate clause applied to a non-private variable}}
  // expected-error@below {{LLVM Translation failed for operation: omp.target}}
  omp.target allocate(%x : !llvm.ptr -> %x : !llvm.ptr) {
    omp.terminator
  }
  llvm.return
}

// -----

llvm.func @target_device(%x : i32) {
  omp.target device(%x : i32) {
    omp.terminator
  }
  llvm.return
}

// -----

omp.declare_reduction @add_f32 : f32
init {
^bb0(%arg: f32):
  %0 = llvm.mlir.constant(0.0 : f32) : f32
  omp.yield (%0 : f32)
}
combiner {
^bb1(%arg0: f32, %arg1: f32):
  %1 = llvm.fadd %arg0, %arg1 : f32
  omp.yield (%1 : f32)
}
atomic {
^bb2(%arg2: !llvm.ptr, %arg3: !llvm.ptr):
  %2 = llvm.load %arg3 : !llvm.ptr -> f32
  llvm.atomicrmw fadd %arg2, %2 monotonic : !llvm.ptr, f32
  omp.yield
}
llvm.func @target_in_reduction(%x : !llvm.ptr) {
  // expected-error@below {{not yet implemented: Unhandled clause in_reduction in omp.target operation}}
  // expected-error@below {{LLVM Translation failed for operation: omp.target}}
  omp.target in_reduction(@add_f32 %x -> %prv : !llvm.ptr) {
    omp.terminator
  }
  llvm.return
}

llvm.func @taskloop_collapse(%lb : i32, %ub : i32, %step : i32, %lb1 : i32, %ub1 : i32, %step1 : i32) {
  omp.taskloop {
    omp.loop_nest (%iv, %iv1) : i32 = (%lb, %lb1) to (%ub, %ub1) inclusive step (%step, %step1) collapse(2) {
      omp.yield
    }
  }
  llvm.return
}

// -----
 omp.declare_reduction @add_reduction_i32 : i32 init {
  ^bb0(%arg0: i32):
    %0 = llvm.mlir.constant(0 : i32) : i32
    omp.yield(%0 : i32)
  }combiner {
  ^bb0(%arg0: i32, %arg1: i32):
    %0 = llvm.add %arg0, %arg1 : i32
    omp.yield(%0 : i32)
  }

llvm.func @taskwait_depend(%x: !llvm.ptr, %len: i32) {
  omp.taskwait depend(taskdependin -> %x : !llvm.ptr size(%len : i32)) {cir.omp.depend_sizes_owned}
  llvm.return
}

// CHECK-CODEGEN-LABEL: define void @taskwait_depend(
// CHECK-CODEGEN-SAME: ptr %{{.*}}, i32 %[[LEN:[^, )]+]])
// CHECK-CODEGEN: %.dep.arr.addr = alloca [1 x %struct.kmp_dep_info], align 8
// CHECK-CODEGEN: %[[LEN64:.+]] = zext i32 %[[LEN]] to i64
// CHECK-CODEGEN: store i64 %[[LEN64]], ptr %{{.+}}, align 4
// CHECK-CODEGEN: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.+}}, i32 1, ptr %.dep.arr.addr, i32 0, ptr null, i32 0)

// -----

llvm.func @taskwait_nowait() {
  omp.taskwait nowait
  llvm.return
}

// CHECK-CODEGEN-LABEL: define void @taskwait_nowait()
// CHECK-CODEGEN: call void @__kmpc_omp_taskwait_deps_51(ptr @{{.+}}, i32 %{{.+}}, i32 0, ptr null, i32 0, ptr null, i32 1)

// -----

llvm.func @teams_allocate(%x : !llvm.ptr) {
  // expected-error@below {{not yet implemented: Unhandled clause allocate in omp.teams operation}}
  // expected-error@below {{LLVM Translation failed for operation: omp.teams}}
  omp.teams allocate(%x : !llvm.ptr -> %x : !llvm.ptr) {
    omp.terminator
  }
  llvm.return
}

// -----

llvm.func @wsloop_allocate(%lb : i32, %ub : i32, %step : i32, %x : !llvm.ptr) {
  // expected-error@below {{omp.wsloop allocate clause applied to a non-private variable}}
  // expected-error@below {{LLVM Translation failed for operation: omp.wsloop}}
  omp.wsloop allocate(%x : !llvm.ptr -> %x : !llvm.ptr) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.yield
    }
  }
  llvm.return
}
