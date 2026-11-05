// RUN: mlir-translate -mlir-to-llvmir -split-input-file -verify-diagnostics %s

// The attribute is a list. Written without the brackets it is still a valid
// attribute, and `getAttrOfType<ArrayAttr>` would hand back null for it -- the
// units would then vanish, while a bad element *inside* a list is an error.

#di_file = #llvm.di_file<"a.c" in "/">
#di_compile_unit = #llvm.di_compile_unit<
  id = distinct[0]<>, sourceLanguage = DW_LANG_C99, file = #di_file,
  isOptimized = false, emissionKind = Full>

// expected-error @below{{expected an array for llvm.dbg.cu}}
module attributes {llvm.dbg.cu = #di_compile_unit} {
}

// -----

#di_file = #llvm.di_file<"a.c" in "/">

// expected-error @below{{expected element 0 of llvm.dbg.cu to be a di_compile_unit, got}}
module attributes {llvm.dbg.cu = [#di_file]} {
}
