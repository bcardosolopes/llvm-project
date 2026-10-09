// RUN: mlir-opt %s -inline='merge-feature-attributes=true' -split-input-file | FileCheck %s
// RUN: mlir-opt %s -inline -split-input-file | FileCheck %s --check-prefix=NOMERGE

// CHECK-LABEL: llvm.func @callee()
// CHECK-SAME: target_features = #llvm.target_features<["+avx", "+sse"]>
// CHECK-LABEL: llvm.func @caller()
// CHECK-SAME: target_features = #llvm.target_features<["+sse", "+neon", "+avx"]>
// CHECK-NOT: llvm.call
// CHECK: llvm.return
// NOMERGE-LABEL: llvm.func @caller()
// NOMERGE-SAME: target_features = #llvm.target_features<["+sse", "+neon"]>
// NOMERGE-NOT: llvm.call
// NOMERGE: llvm.return
llvm.func @callee() attributes {target_features = #llvm.target_features<["+avx", "+sse"]>} {
  llvm.return
}
llvm.func @caller() attributes {target_features = #llvm.target_features<["+sse", "+neon"]>} {
  llvm.call @callee() : () -> ()
  llvm.return
}

// -----

// CHECK-LABEL: llvm.func @feat_callee()
// CHECK-SAME: target_features = #llvm.target_features<["+sve"]>
// CHECK-LABEL: llvm.func @plain_caller()
// CHECK-SAME: target_features = #llvm.target_features<["+sve"]>
// CHECK-NOT: llvm.call
// CHECK: llvm.return
llvm.func @feat_callee() attributes {target_features = #llvm.target_features<["+sve"]>} {
  llvm.return
}
llvm.func @plain_caller() {
  llvm.call @feat_callee() : () -> ()
  llvm.return
}

// -----

// A callee without features leaves the caller untouched, order included.
// CHECK-LABEL: llvm.func @bare_callee()
// CHECK-NOT: target_features
// CHECK-LABEL: llvm.func @feat_caller()
// CHECK-SAME: target_features = #llvm.target_features<["+sse", "+neon"]>
// CHECK-NOT: llvm.call
// CHECK: llvm.return
llvm.func @bare_callee() {
  llvm.return
}
llvm.func @feat_caller() attributes {target_features = #llvm.target_features<["+sse", "+neon"]>} {
  llvm.call @bare_callee() : () -> ()
  llvm.return
}

// -----

// No features anywhere creates no attribute.
// CHECK-LABEL: llvm.func @bare_callee2()
// CHECK-NOT: target_features
// CHECK-LABEL: llvm.func @bare_caller()
// CHECK-NOT: target_features
// CHECK-NOT: llvm.call
// CHECK: llvm.return
llvm.func @bare_callee2() {
  llvm.return
}
llvm.func @bare_caller() {
  llvm.call @bare_callee2() : () -> ()
  llvm.return
}

// -----

// On a +/- conflict the enabled entry wins, exactly once, in either direction.
// CHECK-LABEL: llvm.func @conflict_callee()
// CHECK-SAME: target_features = #llvm.target_features<["-x"]>
// CHECK-LABEL: llvm.func @conflict_caller()
// CHECK-SAME: target_features = #llvm.target_features<["+x", "+y"]>
// CHECK-NOT: llvm.call
// CHECK: llvm.return
llvm.func @conflict_callee() attributes {target_features = #llvm.target_features<["-x"]>} {
  llvm.return
}
llvm.func @conflict_caller() attributes {target_features = #llvm.target_features<["+x", "+y"]>} {
  llvm.call @conflict_callee() : () -> ()
  llvm.return
}

// -----

// CHECK-LABEL: llvm.func @conflict_callee2()
// CHECK-SAME: target_features = #llvm.target_features<["+z"]>
// CHECK-LABEL: llvm.func @conflict_caller2()
// CHECK-SAME: target_features = #llvm.target_features<["+z"]>
// CHECK-NOT: llvm.call
// CHECK: llvm.return
llvm.func @conflict_callee2() attributes {target_features = #llvm.target_features<["+z"]>} {
  llvm.return
}
llvm.func @conflict_caller2() attributes {target_features = #llvm.target_features<["-z"]>} {
  llvm.call @conflict_callee2() : () -> ()
  llvm.return
}

// -----

// A call nested in a region op still merges onto the enclosing function.
// CHECK-LABEL: llvm.func @nested_callee()
// CHECK-SAME: target_features = #llvm.target_features<["+avx"]>
// CHECK-LABEL: llvm.func @nested_caller()
// CHECK-SAME: target_features = #llvm.target_features<["+sse", "+avx"]>
// CHECK-NOT: llvm.call
// CHECK: llvm.return
llvm.func @nested_callee() attributes {target_features = #llvm.target_features<["+avx"]>} {
  llvm.return
}
llvm.func @nested_caller() attributes {target_features = #llvm.target_features<["+sse"]>} {
  scf.execute_region {
    llvm.call @nested_callee() : () -> ()
    scf.yield
  }
  llvm.return
}

// -----

// Each caller of a shared callee merges independently.
// CHECK-LABEL: llvm.func @shared()
// CHECK-SAME: target_features = #llvm.target_features<["+avx"]>
// CHECK-LABEL: llvm.func @c1()
// CHECK-SAME: target_features = #llvm.target_features<["+sse", "+avx"]>
// CHECK-LABEL: llvm.func @c2()
// CHECK-SAME: target_features = #llvm.target_features<["+avx"]>
// CHECK-NOT: llvm.call
llvm.func @shared() attributes {target_features = #llvm.target_features<["+avx"]>} {
  llvm.return
}
llvm.func @c1() attributes {target_features = #llvm.target_features<["+sse"]>} {
  llvm.call @shared() : () -> ()
  llvm.return
}
llvm.func @c2() {
  llvm.call @shared() : () -> ()
  llvm.return
}

// -----

// No merge across dialects: the callee's dialect must not decide attributes
// on a foreign op.
// CHECK-LABEL: llvm.func @llvm_callee()
// CHECK-SAME: target_features = #llvm.target_features<["+avx"]>
// CHECK-LABEL: func.func @func_caller()
// CHECK-NOT: target_features
// CHECK-NOT: llvm.call
// CHECK: return
llvm.func @llvm_callee() attributes {target_features = #llvm.target_features<["+avx"]>} {
  llvm.return
}
func.func @func_caller() {
  llvm.call @llvm_callee() : () -> ()
  return
}
