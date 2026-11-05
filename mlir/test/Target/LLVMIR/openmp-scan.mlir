// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s

omp.declare_reduction @add_f32 : f32
init {
^bb0(%arg: f32):
  %0 = llvm.mlir.constant(0.0 : f32) : f32
  omp.yield (%0 : f32)
}
combiner {
^bb0(%arg0: f32, %arg1: f32):
  %0 = llvm.fadd %arg0, %arg1 : f32
  omp.yield (%0 : f32)
}
atomic {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %0 = llvm.load %arg1 : !llvm.ptr -> f32
  llvm.atomicrmw fadd %arg0, %0 monotonic : !llvm.ptr, f32
  omp.yield
}

llvm.func @scan_wsloop_exclusive(%lb : i32, %ub : i32, %step : i32,
                                 %x : !llvm.ptr) {
  omp.wsloop reduction(mod:inscan, @add_f32 %x -> %prv : !llvm.ptr) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.scan exclusive(%prv : !llvm.ptr)
      omp.yield
    }
  }
  llvm.return
}

// The masked thread allocates the buffer and broadcasts its address.
// CHECK-LABEL: define void @scan_wsloop_exclusive(
// CHECK: store i32 0, ptr %scan.didalloc, align 4
// CHECK: call i32 @__kmpc_masked(
// CHECK: %[[BUF:.*]] = tail call ptr @malloc(
// CHECK: store ptr %[[BUF]], ptr %[[BUFPTR:.*]], align 8
// CHECK: store i32 1, ptr %scan.didalloc, align 4
// CHECK: %[[DIDIT:.*]] = load i32, ptr %scan.didalloc, align 4
// CHECK: call void @__kmpc_copyprivate(ptr @{{.*}}, i32 %{{.*}}, i64 0, ptr %[[BUFPTR]], ptr @.omp.scan.buffer.ptr.copy, i32 %[[DIDIT]])
// CHECK: call void @__kmpc_for_static_init_4u(
// Each input-phase iteration starts from the reduction identity.
// CHECK: omp.inscan.dispatch:
// CHECK: store float 0.000000e+00, ptr %{{.*}}, align 4
// CHECK: call void @__kmpc_barrier(
// CHECK: %[[LOG2:.*]] = call double @llvm.log2.f64(
// CHECK: %[[CEIL:.*]] = call double @llvm.ceil.f64(double %[[LOG2]])
// CHECK: %[[NEUTRAL:.*]] = getelementptr inbounds float, ptr %{{.*}}, i32 0
// CHECK: store float 0.000000e+00, ptr %[[NEUTRAL]], align 4
// CHECK: call void @__kmpc_for_static_init_4u(
// The buffer is freed only after every thread finished the scan phase.
// CHECK: call void @__kmpc_barrier(
// CHECK: call i32 @__kmpc_masked(
// CHECK: tail call void @free(ptr
// CHECK-LABEL: define internal void @.omp.scan.buffer.ptr.copy(ptr %0, ptr %1)
// CHECK: %[[P:.*]] = load ptr, ptr %1, align 8
// CHECK: store ptr %[[P]], ptr %0, align 8

// -----

omp.declare_reduction @add_i32 : i32
init {
^bb0(%arg: i32):
  %0 = llvm.mlir.constant(0 : i32) : i32
  omp.yield (%0 : i32)
}
combiner {
^bb0(%arg0: i32, %arg1: i32):
  %0 = llvm.add %arg0, %arg1 : i32
  omp.yield (%0 : i32)
}
atomic {
^bb0(%arg0: !llvm.ptr, %arg1: !llvm.ptr):
  %0 = llvm.load %arg1 : !llvm.ptr -> i32
  llvm.atomicrmw add %arg0, %0 monotonic : !llvm.ptr, i32
  omp.yield
}

llvm.func @scan_simd_inclusive(%lb : i32, %ub : i32, %step : i32,
                               %x : !llvm.ptr) {
  omp.simd reduction(mod:inscan, @add_i32 %x -> %prv : !llvm.ptr) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.scan inclusive(%prv : !llvm.ptr)
      omp.yield
    }
  }
  llvm.return
}

// CHECK-LABEL: define void @scan_simd_inclusive(
// CHECK: call i32 @__kmpc_masked(
// CHECK: %[[BUF:.*]] = tail call ptr @malloc(
// CHECK: call void @__kmpc_copyprivate(
// CHECK: %[[LOG2:.*]] = call double @llvm.log2.f64(
// CHECK: %[[CEIL:.*]] = call double @llvm.ceil.f64(double %[[LOG2]])
// CHECK: %[[NEUTRAL:.*]] = getelementptr inbounds i32, ptr %{{.*}}, i32 0
// CHECK: store i32 0, ptr %[[NEUTRAL]], align 4
// CHECK: tail call void @free(ptr

// -----

llvm.mlir.global internal @n_elems(0 : i64) : i64

omp.declare_reduction @add_i32_section : !llvm.ptr attributes {byref_element_type = i32} alloc {
  %addr = llvm.mlir.addressof @n_elems : !llvm.ptr
  %n = llvm.load %addr : !llvm.ptr -> i64
  %buf = llvm.alloca %n x i32 : (i64) -> !llvm.ptr
  omp.yield(%buf : !llvm.ptr)
} init {
^bb0(%mold: !llvm.ptr, %alloc: !llvm.ptr):
  %0 = llvm.mlir.constant(0 : i32) : i32
  llvm.store %0, %alloc : i32, !llvm.ptr
  omp.yield(%alloc : !llvm.ptr)
} combiner {
^bb0(%lhs: !llvm.ptr, %rhs: !llvm.ptr):
  %0 = llvm.load %lhs : !llvm.ptr -> i32
  %1 = llvm.load %rhs : !llvm.ptr -> i32
  %2 = llvm.add %0, %1 : i32
  llvm.store %2, %lhs : i32, !llvm.ptr
  omp.yield(%lhs : !llvm.ptr)
}

llvm.func @scan_wsloop_byref(%lb : i32, %ub : i32, %step : i32,
                             %x : !llvm.ptr, %n : i64) {
  %addr = llvm.mlir.addressof @n_elems : !llvm.ptr
  llvm.store %n, %addr : i64, !llvm.ptr
  omp.wsloop reduction(mod:inscan, byref @add_i32_section %x -> %prv : !llvm.ptr) {
    omp.loop_nest (%iv) : i32 = (%lb) to (%ub) step (%step) {
      omp.scan inclusive(%prv : !llvm.ptr)
      omp.yield
    }
  }
  llvm.return
}

// A runtime-sized private copy is allocated where the construct starts, after
// the code that computes its size, and each buffer slot holds a whole copy.
// CHECK-LABEL: define void @scan_wsloop_byref(
// CHECK: store i64 %4, ptr @n_elems, align 4
// CHECK: %[[N:.*]] = load i64, ptr @n_elems, align 4
// CHECK: %[[PRV:.*]] = alloca i32, i64 %[[N]], align 4
// CHECK: %[[SIZE:omp.scan.elem.size]] = mul i64 %[[N]], 4
// CHECK: call ptr @malloc(i64
// CHECK: %[[STORE_SLOT:omp.scan.slot]] = getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}
// CHECK: call void @llvm.memcpy.p0.p0.i64(ptr %[[STORE_SLOT]], ptr %[[PRV]], i64 %[[SIZE]], i1 false)
// The prefix reduction combines whole slots in place.
// CHECK: %[[LHS:omp.scan.slot[0-9]+]] = getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}
// CHECK: %[[RHS:omp.scan.slot[0-9]+]] = getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}
// CHECK: store i32 %{{.*}}, ptr %[[LHS]], align 4
// CHECK: %[[LOAD_SLOT:omp.scan.slot[0-9]+]] = getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}
// CHECK: call void @llvm.memcpy.p0.p0.i64(ptr %[[PRV]], ptr %[[LOAD_SLOT]], i64 %[[SIZE]], i1 false)
