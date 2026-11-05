// RUN: mlir-translate -mlir-to-llvmir -split-input-file -verify-diagnostics %s

// `alloc_size` is a `DenseI32ArrayAttr` and there is no op verifier behind it,
// so nothing but this checks what is in it. Both of LLVM's own guards are
// assertions, which means a bad value crashes an assert-enabled build and
// silently corrupts the attribute in a release one -- neither reachable from
// an imported module, but both reachable from hand-written or rewritten IR.

llvm.func @callee() -> !llvm.ptr

llvm.func @alloc_size_too_many() {
  // expected-error @below{{expected one or two indices in alloc_size}}
  // expected-error @below{{LLVM Translation failed for operation: llvm.call}}
  %0 = llvm.call @callee() {alloc_size = array<i32: 0, 1, 2>} : () -> !llvm.ptr
  llvm.return
}

// -----

llvm.func @callee() -> !llvm.ptr

llvm.func @alloc_size_empty() {
  // expected-error @below{{expected one or two indices in alloc_size}}
  // expected-error @below{{LLVM Translation failed for operation: llvm.call}}
  %0 = llvm.call @callee() {alloc_size = array<i32>} : () -> !llvm.ptr
  llvm.return
}

// -----

// `-1` becomes `0xFFFFFFFF`, which is LLVM's "no second index" sentinel: the
// assertion in `packAllocSizeArgs` catches it, and a release build would
// quietly drop the second index instead.
llvm.func @callee() -> !llvm.ptr

llvm.func @alloc_size_negative() {
  // expected-error @below{{expected non-negative indices in alloc_size}}
  // expected-error @below{{LLVM Translation failed for operation: llvm.call}}
  %0 = llvm.call @callee() {alloc_size = array<i32: 0, -1>} : () -> !llvm.ptr
  llvm.return
}

// -----

llvm.func @callee() -> !llvm.ptr

llvm.func @alloc_size_zero_zero() {
  // expected-error @below{{expected alloc_size other than (0, 0)}}
  // expected-error @below{{LLVM Translation failed for operation: llvm.call}}
  %0 = llvm.call @callee() {alloc_size = array<i32: 0, 0>} : () -> !llvm.ptr
  llvm.return
}

// -----

// The indices name arguments, so one past the end names an argument that does
// not exist. LLVM's verifier rejects that; catching it here points the
// diagnostic at the operation instead.
llvm.func @callee_one_arg(i64) -> !llvm.ptr

llvm.func @alloc_size_out_of_range(%n: i64) {
  // expected-error @below{{alloc_size index 3 is out of range for 1 callee parameters}}
  // expected-error @below{{LLVM Translation failed for operation: llvm.call}}
  %0 = llvm.call @callee_one_arg(%n) {
    alloc_size = array<i32: 3>
  } : (i64) -> !llvm.ptr
  llvm.return
}

// -----

// An indirect call leads with the callee pointer, so counting operands would
// make the bound one too generous. Index 1 names the second *parameter*, of
// which there is one.
llvm.func @indirect(%fn: !llvm.ptr, %n: i64) {
  // expected-error @below{{alloc_size index 1 is out of range for 1 callee parameters}}
  // expected-error @below{{LLVM Translation failed for operation: llvm.call}}
  %0 = llvm.call %fn(%n) {
    alloc_size = array<i32: 1>
  } : !llvm.ptr, (i64) -> !llvm.ptr
  llvm.return
}

// -----

// A variadic callee's trailing arguments are not parameters, so they cannot be
// named either -- which is what LLVM's own check compares against.
llvm.func @varargs(i64, ...) -> !llvm.ptr

llvm.func @variadic_index(%n: i64, %m: i64) {
  // expected-error @below{{alloc_size index 1 is out of range for 1 callee parameters}}
  // expected-error @below{{LLVM Translation failed for operation: llvm.call}}
  %0 = llvm.call @varargs(%n, %m) vararg(!llvm.func<ptr (i64, ...)>) {
    alloc_size = array<i32: 1>
  } : (i64, i64) -> !llvm.ptr
  llvm.return
}

// -----

// A size is a number, so the parameter an index names has to be an integer --
// which LLVM checks alongside the bound.
llvm.func @callee_ptr_arg(!llvm.ptr) -> !llvm.ptr

llvm.func @alloc_size_non_integer(%p: !llvm.ptr) {
  // expected-error @below{{alloc_size index 0 does not name an integer parameter}}
  // expected-error @below{{LLVM Translation failed for operation: llvm.call}}
  %0 = llvm.call @callee_ptr_arg(%p) {
    alloc_size = array<i32: 0>
  } : (!llvm.ptr) -> !llvm.ptr
  llvm.return
}

// -----

// The same writer serves the invoke, so a bad value there is rejected too.
llvm.func @callee() -> !llvm.ptr
llvm.func @__gxx_personality_v0(...) -> i32

llvm.func @alloc_size_on_invoke() -> !llvm.ptr
    attributes {personality = @__gxx_personality_v0} {
  // expected-error @below{{expected non-negative indices in alloc_size}}
  // expected-error @below{{LLVM Translation failed for operation: llvm.invoke}}
  %0 = llvm.invoke @callee() to ^bb1 unwind ^bb2 {
    alloc_size = array<i32: -1>
  } : () -> !llvm.ptr
^bb1:
  llvm.return %0 : !llvm.ptr
^bb2:
  %1 = llvm.landingpad cleanup : !llvm.struct<(ptr, i32)>
  %2 = llvm.mlir.zero : !llvm.ptr
  llvm.return %2 : !llvm.ptr
}
