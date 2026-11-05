; RUN: mlir-translate -import-llvm -split-input-file %s | FileCheck %s

; `!annotation` is modelled by AnnotationOpInterface, carried as the typed
; `annotations` attribute. LLVM permits the metadata on any instruction and
; clang emits it on both stores and memset calls for -ftrivial-auto-var-init,
; so the interface is declared on the memory operations and the memory
; intrinsics rather than on one carrier.

declare void @sink(ptr)

; CHECK-LABEL: @annotation_flat
define void @annotation_flat(ptr %p) {
  ; CHECK: llvm.store
  ; CHECK-SAME: annotations = ["auto-init"]
  store i32 7, ptr %p, align 4, !annotation !0
  ; CHECK: llvm.call @sink
  ; CHECK-SAME: annotations = ["auto-init"]
  call void @sink(ptr %p), !annotation !0
  ret void
}

!0 = !{!"auto-init"}

; // -----

declare void @sink(ptr)

; An entry may itself be a tuple of strings; both spellings survive.
; CHECK-LABEL: @annotation_nested
define void @annotation_nested(ptr %p) {
  ; CHECK: llvm.call @sink
  ; `[[` opens a FileCheck variable, so the outer bracket is escaped.
  ; CHECK-SAME: annotations = {{\[\[}}"kind", "detail"], "plain"]
  call void @sink(ptr %p), !annotation !0
  ret void
}

!0 = !{!{!"kind", !"detail"}, !"plain"}
