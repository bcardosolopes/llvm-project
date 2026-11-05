// RUN: mlir-translate -mlir-to-llvmir -split-input-file -verify-diagnostics %s

// These are discardable attributes, so nothing verifies them before export.
// Each of these inputs is valid MLIR that LLVM's own verifier would reject, so
// without a check here the failure surfaces as a module-level verifier error
// with nothing pointing back at the attribute that caused it.

llvm.func @fpmath_zero(%x: f32) -> f32 {
  // expected-error @below{{expected a positive finite value for llvm.fpmath}}
  %0 = llvm.fadd %x, %x {llvm.fpmath = 0.000000e+00 : f32} : f32
  llvm.return %0 : f32
}

// -----

llvm.func @fpmath_negative(%x: f32) -> f32 {
  // expected-error @below{{expected a positive finite value for llvm.fpmath}}
  %0 = llvm.fadd %x, %x {llvm.fpmath = -2.500000e+00 : f32} : f32
  llvm.return %0 : f32
}

// -----

// A double too small to survive the narrowing to `float` would emit a zero.
llvm.func @fpmath_flushes_to_zero(%x: f32) -> f32 {
  // expected-error @below{{expected a positive finite value for llvm.fpmath}}
  %0 = llvm.fadd %x, %x {llvm.fpmath = 1.000000e-300 : f64} : f32
  llvm.return %0 : f32
}

// -----

llvm.func @align_zero(%p: !llvm.ptr) -> !llvm.ptr {
  // expected-error @below{{expected a power of two no greater than}}
  %0 = llvm.load %p {llvm.align_metadata = 0 : i64} : !llvm.ptr -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// -----

llvm.func @align_not_a_power_of_two(%p: !llvm.ptr) -> !llvm.ptr {
  // expected-error @below{{expected a power of two no greater than}}
  %0 = llvm.load %p {llvm.align_metadata = 24 : i64} : !llvm.ptr -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// -----

// A power of two, but past `Value::MaximumAlignment`.
llvm.func @align_too_large(%p: !llvm.ptr) -> !llvm.ptr {
  // expected-error @below{{expected a power of two no greater than}}
  %0 = llvm.load %p {
    llvm.align_metadata = 4611686018427387904 : i64
  } : !llvm.ptr -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// -----

llvm.func @noalias_addrspace_empty(%p: !llvm.ptr) -> !llvm.ptr {
  // expected-error @below{{expected at least one range in llvm.noalias_addrspace}}
  %0 = llvm.load %p {llvm.noalias_addrspace = []} : !llvm.ptr -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// -----

llvm.func @noalias_addrspace_not_i32(%p: !llvm.ptr) -> !llvm.ptr {
  // expected-error @below{{expected 32-bit bounds in llvm.noalias_addrspace}}
  %0 = llvm.load %p {
    llvm.noalias_addrspace = [#llvm.constant_range<i64, 1, 2>]
  } : !llvm.ptr -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// -----

llvm.func @noalias_addrspace_equal_bounds(%p: !llvm.ptr) -> !llvm.ptr {
  // expected-error @below{{expected distinct bounds in llvm.noalias_addrspace}}
  %0 = llvm.load %p {
    llvm.noalias_addrspace = [#llvm.constant_range<i32, 3, 3>]
  } : !llvm.ptr -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// -----

llvm.func @noalias_addrspace_overlapping(%p: !llvm.ptr) -> !llvm.ptr {
  // expected-error @below{{expected non-overlapping ranges in llvm.noalias_addrspace}}
  %0 = llvm.load %p {
    llvm.noalias_addrspace = [#llvm.constant_range<i32, 1, 5>,
                              #llvm.constant_range<i32, 3, 8>]
  } : !llvm.ptr -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// -----

llvm.func @noalias_addrspace_out_of_order(%p: !llvm.ptr) -> !llvm.ptr {
  // expected-error @below{{expected ranges in increasing order in llvm.noalias_addrspace}}
  %0 = llvm.load %p {
    llvm.noalias_addrspace = [#llvm.constant_range<i32, 5, 6>,
                              #llvm.constant_range<i32, 1, 2>]
  } : !llvm.ptr -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// -----

// Past two ranges the last one can wrap around into the first, which the
// pairwise checks never compare -- `[10, 2)` runs off the top and back to 1,
// straight through `[1, 3)`.
llvm.func @noalias_addrspace_wraps_into_first(%p: !llvm.ptr) -> !llvm.ptr {
  // expected-error @below{{expected non-overlapping ranges in llvm.noalias_addrspace}}
  %0 = llvm.load %p {
    llvm.noalias_addrspace = [#llvm.constant_range<i32, 1, 3>,
                              #llvm.constant_range<i32, 5, 7>,
                              #llvm.constant_range<i32, 10, 2>]
  } : !llvm.ptr -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// -----

// And the same wrap landing exactly on the first range's lower bound, which is
// contiguous rather than overlapping.
llvm.func @noalias_addrspace_wraps_onto_first(%p: !llvm.ptr) -> !llvm.ptr {
  // expected-error @below{{expected non-contiguous ranges in llvm.noalias_addrspace}}
  %0 = llvm.load %p {
    llvm.noalias_addrspace = [#llvm.constant_range<i32, 1, 3>,
                              #llvm.constant_range<i32, 5, 7>,
                              #llvm.constant_range<i32, 10, 1>]
  } : !llvm.ptr -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// -----

// `[1, 3)` and `[3, 6)` are one range, and LLVM wants it written as one.
llvm.func @noalias_addrspace_contiguous(%p: !llvm.ptr) -> !llvm.ptr {
  // expected-error @below{{expected non-contiguous ranges in llvm.noalias_addrspace}}
  %0 = llvm.load %p {
    llvm.noalias_addrspace = [#llvm.constant_range<i32, 1, 3>,
                              #llvm.constant_range<i32, 3, 6>]
  } : !llvm.ptr -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}
