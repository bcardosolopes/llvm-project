; RUN: mlir-translate -import-llvm %s | FileCheck %s

; `CallBase::getMemoryEffects()` folds in the callee's own effects --
; `ME &= Fn->getMemoryEffects()` -- so reading it would give a call site with
; no `memory(...)` of its own the declaration's, and the export would then
; write that onto the call. The effects are read from the call's attribute list
; instead, which is only what the call site itself says.

declare void @ro() memory(read)
declare void @plain()

; CHECK-LABEL: @inherits_nothing
define void @inherits_nothing() {
  ; The callee is `memory(read)` and the call says nothing, so the call gains
  ; nothing.
  ; CHECK: llvm.call @ro() : () -> ()
  call void @ro()
  ret void
}

; CHECK-LABEL: @keeps_its_own
define void @keeps_its_own() {
  ; An effect the call site really does carry is still kept, and is not
  ; narrowed by the callee's.
  ; CHECK: llvm.call @ro()
  ; CHECK-SAME: argMem = write
  call void @ro() memory(argmem: write)
  ret void
}
