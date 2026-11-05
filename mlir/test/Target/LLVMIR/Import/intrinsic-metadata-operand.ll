; RUN: not mlir-translate -import-llvm %s 2>&1 | FileCheck %s

; A call with a metadata operand. `convertValue` asserts on metadata rather
; than declining, so this used to abort the importer outright:
;
;   Assertion `!isa<llvm::MetadataAsValue>(value) && "expected value to not be
;   metadata"' failed.
;
; It is now declined with a reason, which is what lets the rest of the import
; finish and the census count the loss.
;
; The example is a *declared but unknown* `llvm.`-prefixed function rather than
; a real intrinsic, deliberately. It used to be `llvm.allow.runtime.check`,
; then constrained `fadd`, then constrained `fcmp` -- each moved on as it was
; modelled, and every intrinsic taking a metadata operand now has an op. A name
; LLVM does not know cannot be modelled, so it cannot go stale, and it takes
; the ordinary call path -- which is the path this diagnostic lives on.

; CHECK: error: unhandled instruction: a metadata operand has no dialect representation

declare i32 @llvm.not.a.real.intrinsic(metadata)

define void @metadata_operand() {
  %r = call i32 @llvm.not.a.real.intrinsic(metadata !"x")
  ret void
}
