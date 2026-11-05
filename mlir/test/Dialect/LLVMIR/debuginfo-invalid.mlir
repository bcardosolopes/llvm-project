// RUN: mlir-opt %s -split-input-file -verify-diagnostics

// expected-error @below {{value of a debug info annotation must be a StringAttr or an IntegerAttr}}
#ann = #llvm.di_annotation<name = "bad", value = 1.0 : f32>

// -----

// expected-error @below {{integer value of a debug info annotation must have a signless integer type}}
#ann = #llvm.di_annotation<name = "bad", value = 1 : si32>
