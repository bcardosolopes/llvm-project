// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s

// Test that connectPHINodes handles well-formed PHI nodes where types
// match. When CIR-to-LLVM lowering produces block arguments whose LLVM
// IR types diverge across branches (e.g., ptr vs i64 due to ABI
// coercion), connectPHINodes inserts appropriate casts.  The MLIR
// verifier prevents constructing mismatched types directly, so this
// test verifies the non-cast (matching-type) path works correctly.

// -----

// CHECK-LABEL: @test_phi_ptr_matching
llvm.func @test_phi_ptr_matching(%arg0: i1, %arg1: !llvm.ptr, %arg2: !llvm.ptr) -> !llvm.ptr {
  llvm.cond_br %arg0, ^bb1(%arg1 : !llvm.ptr), ^bb1(%arg2 : !llvm.ptr)
^bb1(%0: !llvm.ptr):
  // CHECK: phi ptr
  llvm.return %0 : !llvm.ptr
}

// -----

// CHECK-LABEL: @test_phi_int_matching
llvm.func @test_phi_int_matching(%arg0: i1, %arg1: i64, %arg2: i64) -> i64 {
  llvm.cond_br %arg0, ^bb1(%arg1 : i64), ^bb1(%arg2 : i64)
^bb1(%0: i64):
  // CHECK: phi i64
  llvm.return %0 : i64
}

// -----

// Multiple PHI nodes in the same block.
// CHECK-LABEL: @test_multi_phi
llvm.func @test_multi_phi(%cond: i1, %a1: i32, %a2: !llvm.ptr, %b1: i32, %b2: !llvm.ptr) -> i32 {
  llvm.cond_br %cond, ^bb1(%a1, %a2 : i32, !llvm.ptr), ^bb1(%b1, %b2 : i32, !llvm.ptr)
^bb1(%r1: i32, %r2: !llvm.ptr):
  // CHECK: phi i32
  // CHECK: phi ptr
  llvm.return %r1 : i32
}
