// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s
// RUN: mlir-opt --verify-roundtrip -split-input-file %s

llvm.func @assertions(%p: !llvm.ptr, %x: f32, %q: !llvm.ptr) -> !llvm.ptr {
  // CHECK: load ptr, ptr %{{.*}}, align 8, !align ![[ALIGN:[0-9]+]], !noalias.addrspace ![[NAAS:[0-9]+]]
  %0 = llvm.load %p {
    llvm.align_metadata = 16 : i64,
    llvm.noalias_addrspace = [#llvm.constant_range<i32, 1, 2>,
                              #llvm.constant_range<i32, 5, 6>]
  } : !llvm.ptr -> !llvm.ptr
  // CHECK: fadd float %{{.*}}, !fpmath ![[FPMATH:[0-9]+]]
  %1 = llvm.fadd %x, %x {llvm.fpmath = 2.500000e+00 : f32} : f32
  llvm.return %0 : !llvm.ptr
}

// CHECK-DAG: ![[ALIGN]] = !{i64 16}
// The two ranges stay separate. Unioning them would claim address spaces 2, 3
// and 4 as well, which the metadata never said.
// CHECK-DAG: ![[NAAS]] = !{i32 1, i32 2, i32 5, i32 6}
// CHECK-DAG: ![[FPMATH]] = !{float 2.500000e+00}

// -----

// The other four carriers the verifier accepts for `!noalias.addrspace`. Each
// gets its own `isa` arm in the exporter, so each needs its own check.

llvm.func @callee(!llvm.ptr) -> i32

// CHECK-LABEL: @memory_carriers
llvm.func @memory_carriers(%p: !llvm.ptr, %v: i32) {
  // CHECK: store i32 %{{.*}}, ptr %{{.*}}, align 4, !noalias.addrspace ![[NAAS:[0-9]+]]
  llvm.store %v, %p {
    alignment = 4 : i64,
    llvm.noalias_addrspace = [#llvm.constant_range<i32, 1, 2>,
                              #llvm.constant_range<i32, 5, 6>]
  } : i32, !llvm.ptr
  // CHECK: atomicrmw add ptr %{{.*}}, i32 %{{.*}} monotonic, align 4, !noalias.addrspace ![[NAAS]]
  %0 = llvm.atomicrmw add %p, %v monotonic {
    llvm.noalias_addrspace = [#llvm.constant_range<i32, 1, 2>,
                              #llvm.constant_range<i32, 5, 6>]
  } : !llvm.ptr, i32
  // CHECK: cmpxchg ptr %{{.*}}, i32 %{{.*}}, i32 %{{.*}} acq_rel monotonic, align 4, !noalias.addrspace ![[NAAS]]
  %1 = llvm.cmpxchg %p, %v, %0 acq_rel monotonic {
    llvm.noalias_addrspace = [#llvm.constant_range<i32, 1, 2>,
                              #llvm.constant_range<i32, 5, 6>]
  } : !llvm.ptr, i32
  // CHECK: call i32 @callee(ptr %{{.*}}), !noalias.addrspace ![[NAAS]]
  %2 = llvm.call @callee(%p) {
    llvm.noalias_addrspace = [#llvm.constant_range<i32, 1, 2>,
                              #llvm.constant_range<i32, 5, 6>]
  } : (!llvm.ptr) -> i32
  llvm.return
}

// CHECK: ![[NAAS]] = !{i32 1, i32 2, i32 5, i32 6}

// -----

// An invoke is NOT a legal carrier: `Verifier::visitInstruction` accepts
// `!noalias.addrspace` on a `CallInst` but not on an `InvokeInst` or a
// `CallBrInst`. Exporting it there would emit IR the verifier rejects, so the
// attribute is dropped rather than written onto an illegal carrier.

llvm.func @callee(!llvm.ptr) -> i32
llvm.func @__gxx_personality_v0(...) -> i32

// CHECK-LABEL: @invoke_is_not_a_carrier
llvm.func @invoke_is_not_a_carrier(%p: !llvm.ptr) -> i32
    attributes {personality = @__gxx_personality_v0} {
  // CHECK: invoke i32 @callee(ptr %{{.*}})
  // CHECK-NOT: !noalias.addrspace
  %0 = llvm.invoke @callee(%p) to ^bb1 unwind ^bb2 {
    llvm.noalias_addrspace = [#llvm.constant_range<i32, 1, 2>,
                              #llvm.constant_range<i32, 5, 6>]
  } : (!llvm.ptr) -> i32
^bb1:
  llvm.return %0 : i32
^bb2:
  %1 = llvm.landingpad cleanup : !llvm.struct<(ptr, i32)>
  %2 = llvm.mlir.constant(0 : i32) : i32
  llvm.return %2 : i32
}
