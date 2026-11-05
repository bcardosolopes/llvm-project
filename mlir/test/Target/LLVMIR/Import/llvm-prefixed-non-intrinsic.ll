; RUN: not mlir-translate -import-llvm %s 2>&1 | FileCheck %s

; `@llvm.random.metadata.use` is not a real intrinsic -- it is an ordinary
; declaration whose name happens to start with `llvm.`. `IntrinsicInst::classof`
; only asks about that prefix (`Function::isIntrinsic` returns
; `HasLLVMReservedName`), so the call reached the intrinsic path carrying
; `not_intrinsic` as its ID and tripped an assertion in `LLVMImportInterface`:
;
;   Assertion `intrinId != llvm::Intrinsic::not_intrinsic' failed.
;
; It is a plain call as far as the dialect is concerned, so it now takes the
; plain call path. The metadata operand still has no representation, so the
; call is declined -- but with a diagnostic naming the reason, rather than an
; abort or, as an intermediate state of this change, a silent exit.

; CHECK: error: unhandled instruction: a metadata operand has no dialect representation: {{.*}}llvm.random.metadata.use

declare void @llvm.random.metadata.use(metadata)

define void @uses_it(ptr %p) {
  call void @llvm.random.metadata.use(metadata ptr %p)
  ret void
}
