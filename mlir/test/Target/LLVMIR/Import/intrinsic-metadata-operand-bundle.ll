; RUN: not mlir-translate -import-llvm %s 2>&1 | FileCheck %s

; A modelled intrinsic carrying metadata in an *operand bundle* rather than in
; an argument. `convertIntrinsicArguments` walks `bundle.Inputs` in a separate
; loop from the arguments, so guarding the arguments alone still left this
; reaching `convertValue`, which asserts:
;
;   Assertion `!isa<llvm::MetadataAsValue>(value) && "expected value to not be
;   metadata"' failed.
;
; `opt -passes=verify` accepts this input, and llvm/test/Bitcode/operand-bundles.ll
; pins metadata bundle operands as valid IR.

; CHECK-NOT: unhandled value
; CHECK: error: unhandled intrinsic: {{.*}}llvm.assume

declare void @llvm.assume(i1)

define void @assume_metadata_bundle() {
  call void @llvm.assume(i1 true) [ "ignore"(metadata !"x") ]
  ret void
}
