// RUN: mlir-translate -mlir-to-llvmir -split-input-file -verify-diagnostics %s

// `llvm.inline_asm` checks its call-site attributes with the same helpers as
// `llvm.call`: a fact with its own attribute cannot also be in `passthrough`.

llvm.func @modelled_in_passthrough() {
  // expected-error @below{{'nounwind' has its own attribute on this operation and cannot also appear in 'passthrough'}}
  // expected-error @below{{LLVM Translation failed for operation: llvm.inline_asm}}
  llvm.inline_asm has_side_effects {passthrough = ["nounwind"]} "nop", "" : () -> ()
  llvm.return
}

// -----

llvm.func @alloc_size_out_of_range(%n: i64) {
  // expected-error @below{{alloc_size index 1 is out of range for 1 callee parameters}}
  // expected-error @below{{LLVM Translation failed for operation: llvm.inline_asm}}
  %0 = llvm.inline_asm {alloc_size = array<i32: 1>} "", "=r,r" %n : (i64) -> !llvm.ptr
  llvm.return
}
