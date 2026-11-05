// RUN: mlir-opt %s -split-input-file -verify-diagnostics | FileCheck %s
// RUN: mlir-opt %s -split-input-file -verify-diagnostics --verify-roundtrip -o /dev/null

// CHECK-LABEL: llvm.func @void_asm_goto
llvm.func @void_asm_goto(%arg0: i32) -> i32 {
  // CHECK: llvm.callbr has_side_effects asm_dialect = att "jmp ${1:l}", "r,!i"(%{{.*}}) : (i32) -> () to ^[[FALL:.*]] [^[[TARGET:.*]]]
  llvm.callbr has_side_effects asm_dialect = att "jmp ${1:l}", "r,!i"(%arg0) : (i32) -> () to ^bb1 [^bb2]
^bb1:
  llvm.return %arg0 : i32
^bb2:
  %0 = llvm.mlir.constant(1 : i32) : i32
  llvm.return %0 : i32
}

// -----

// CHECK-LABEL: llvm.func @result_and_block_arguments
llvm.func @result_and_block_arguments(%arg0: i32) -> i32 {
  %0 = llvm.mlir.constant(7 : i32) : i32
  // CHECK: %[[RES:.*]] = llvm.callbr "mov $1, $0; jmp ${2:l}; jmp ${3:l}", "=r,r,!i,!i"(%{{.*}}) : (i32) -> i32 to ^{{.*}}(%{{.*}} : i32) [^{{.*}}(%{{.*}}, %{{.*}} : i32, i32), ^{{.*}}]
  %1 = llvm.callbr "mov $1, $0; jmp ${2:l}; jmp ${3:l}", "=r,r,!i,!i"(%arg0) : (i32) -> i32 to ^bb1(%0 : i32) [^bb2(%0, %arg0 : i32, i32), ^bb3]
^bb1(%2: i32):
  // The result is available in the default destination.
  %3 = llvm.add %1, %2 : i32
  llvm.return %3 : i32
^bb2(%4: i32, %5: i32):
  // And in every indirect destination.
  %6 = llvm.add %1, %4 : i32
  llvm.return %6 : i32
^bb3:
  llvm.return %1 : i32
}

// -----

// CHECK-LABEL: llvm.func @call_site_attributes
llvm.func @call_site_attributes(%arg0: !llvm.ptr) {
  // CHECK: llvm.callbr has_side_effects operand_attrs = [{elementtype = i32}] "", "*m,!i"(%{{.*}}) : (!llvm.ptr) -> () to ^{{.*}} [^{{.*}}] {no_unwind, srcloc = array<i64: 12>}
  llvm.callbr has_side_effects operand_attrs = [{elementtype = i32}] "", "*m,!i"(%arg0) : (!llvm.ptr) -> () to ^bb1 [^bb1] {no_unwind, srcloc = array<i64: 12>}
^bb1:
  llvm.return
}

// -----

llvm.func @label_count_mismatch(%arg0: i32) {
  // expected-error@+1 {{expects one indirect destination per label constraint, got 2 for 1}}
  llvm.callbr "", "r,!i"(%arg0) : (i32) -> () to ^bb1 [^bb1, ^bb1]
^bb1:
  llvm.return
}
