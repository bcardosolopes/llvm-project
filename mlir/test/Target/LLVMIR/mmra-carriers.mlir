// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

// `!mmra` is legal only on the memory-access, atomic and fence instructions --
// `canInstructionHaveMMRAs` in LLVM. The attribute rides on the operation, so
// an operation that expands into several instructions must not have it written
// onto the ones that cannot carry it; that is IR the verifier rejects.

#mmra_tag = #llvm.mmra_tag<"opencl-fence-mem":"global">

llvm.func @carriers(%p: !llvm.ptr, %v: i32) {
  // A load can carry it.
  // CHECK: load i32, ptr %{{.*}}, align 4, !mmra ![[TAG:[0-9]+]]
  %0 = llvm.load %p {
    alignment = 4 : i64,
    llvm.mmra = #mmra_tag
  } : !llvm.ptr -> i32
  // A fence can carry it.
  // CHECK: fence release, !mmra ![[TAG]]
  llvm.fence release {llvm.mmra = #mmra_tag}
  llvm.return
}

// An `add` is not one of the carriers, so it gains nothing rather than being
// written IR the verifier would reject.
//
// Plain `CHECK`, not `CHECK-LABEL`: a label starts a new block and `TAG` would
// not be in scope for the definition below it.
// CHECK: define i32 @not_a_carrier
llvm.func @not_a_carrier(%x: i32, %y: i32) -> i32 {
  // CHECK-NOT: !mmra
  %0 = llvm.add %x, %y {llvm.mmra = #mmra_tag} : i32
  llvm.return %0 : i32
}

// CHECK: ![[TAG]] = !{!"opencl-fence-mem", !"global"}
