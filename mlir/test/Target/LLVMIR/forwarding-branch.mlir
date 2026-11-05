// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

// A branch tagged as a forwarding branch keeps the tag as metadata.
// CHECK-LABEL: define i32 @tagged(
// CHECK: br label %{{[0-9]+}}, !llvm_forwarding_branch ![[$EMPTY:[0-9]+]]
llvm.func @tagged(%arg0: i32) -> i32 {
  llvm.br ^bb1(%arg0 : i32) {llvm_forwarding_branch}
^bb1(%0: i32):
  llvm.return %0 : i32
}

// LLVM IR repeats a successor that MLIR passes different arguments to; the
// export legalization gives each repeat a forwarding block and tags its branch.
// CHECK-LABEL: define i32 @repeated_successor(
// CHECK: switch i8
// CHECK: br label %{{[0-9]+}}, !llvm_forwarding_branch ![[$EMPTY]]
llvm.func @repeated_successor(%arg0: i8) -> i32 {
  %one = llvm.mlir.constant(1 : i32) : i32
  %two = llvm.mlir.constant(2 : i32) : i32
  llvm.switch %arg0 : i8, ^bb2 [
    1: ^bb1(%one : i32),
    2: ^bb1(%two : i32)
  ]
^bb1(%x: i32):
  llvm.return %x : i32
^bb2:
  llvm.return %one : i32
}

// CHECK: ![[$EMPTY]] = !{}
