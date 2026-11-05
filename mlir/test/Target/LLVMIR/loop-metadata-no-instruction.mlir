// RUN: not mlir-translate -mlir-to-llvmir -split-input-file %s 2>&1 | FileCheck %s
// RUN: mlir-opt %s -split-input-file --verify-roundtrip -o /dev/null

// Import accepts `llvm.loop_annotation` on any carrier, so export has to say
// something when a carrier turns out to produce no LLVM instruction to hang it
// on -- otherwise the loss simply moves from import, where it is reported, to
// export, where it would be silent.
//
// llvm.mlir.constant becomes an llvm::Constant rather than an Instruction, so
// it is a real instance of that case rather than a synthetic one.

#loop_annotation = #llvm.loop_annotation<mustProgress = true>

// CHECK: warning: dropped !llvm.loop: the operation produced no LLVM instruction
llvm.func @constant_carrier() -> i32 {
  %0 = llvm.mlir.constant(0 : i32) {llvm.loop_annotation = #loop_annotation} : i32
  llvm.return %0 : i32
}

// -----

// An ill-formed attribute is an error rather than a silent skip.

// CHECK: error: 'llvm.mlir.constant' op expected a loop annotation in llvm.loop_annotation
llvm.func @wrong_attribute_type() -> i32 {
  %0 = llvm.mlir.constant(0 : i32) {llvm.loop_annotation = "not-an-annotation"} : i32
  llvm.return %0 : i32
}
