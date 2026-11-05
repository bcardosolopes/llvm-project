// RUN: mlir-opt %s -split-input-file -verify-diagnostics

// The parameters of a MemInfoBlock are untyped arrays, so the shape LLVM's
// verifier requires has to be checked here rather than by the parser.

// expected-error @below {{call stack of a memprof MemInfoBlock must not be empty}}
#mib = #llvm.memprof_mib<call_stack = [], tags = ["cold"]>

// -----

// expected-error @below {{a memprof MemInfoBlock must have at least one allocation type tag}}
#mib = #llvm.memprof_mib<call_stack = [1], tags = []>

// -----

// expected-error @below {{allocation type tags of a memprof MemInfoBlock must be StringAttrs}}
#mib = #llvm.memprof_mib<call_stack = [1], tags = [42]>

// -----

// expected-error @below {{context sizes of a memprof MemInfoBlock must be memprof_context_size attributes}}
#mib = #llvm.memprof_mib<call_stack = [1], tags = ["cold"], context_sizes = [42]>
