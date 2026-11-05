; RUN: mlir-translate -import-llvm -emit-expensive-warnings -split-input-file %s 2>&1 | FileCheck %s

; !callees lists the functions an indirect call may reach. ModuleSummaryAnalysis
; reads it to drive indirect call promotion -- exactly the kind of cross-library
; information this pipeline exists to preserve.

; CHECK-NOT: dropped metadata

declare void @a()
declare void @b()

; Each callee becomes a symbol reference rather than a copied name, so it
; participates in MLIR symbol renaming instead of going stale.
; CHECK-LABEL: llvm.func @indirect_call
; CHECK: llvm.call %{{.*}}() {callees = [@a, @b]}
define void @indirect_call(ptr %fp) {
  call void %fp(), !callees !0
  ret void
}

!0 = !{ptr @a, ptr @b}

; // -----

; An invoke is a call with an unwind edge and carries the same node.

declare void @a()
declare i32 @__gxx_personality_v0(...)

; CHECK-LABEL: llvm.func @indirect_invoke
; CHECK: llvm.invoke %{{.*}}() to ^bb1 unwind ^bb2 {callees = [@a]}
define void @indirect_invoke(ptr %fp) personality ptr @__gxx_personality_v0 {
entry:
  invoke void %fp() to label %done unwind label %lpad, !callees !0
done:
  ret void
lpad:
  %ex = landingpad { ptr, i32 } cleanup
  ret void
}

!0 = !{ptr @a}

; // -----

; A null operand is what RAUW leaves behind when a listed callee goes away, and
; LTO does that constantly. Reading it with a plain dyn_cast asserted, which
; crashed the importer on a real link.

declare void @a()

; CHECK-LABEL: llvm.func @callees_with_null
; CHECK: llvm.call %{{.*}}() {callees = [@a]}
define void @callees_with_null(ptr %fp) {
  call void %fp(), !callees !0
  ret void
}

!0 = !{ptr @a, null}

; // -----

; A node that is nothing but null operands leaves no callees at all. Declining it
; is honest -- an empty attribute would claim the call reaches nothing, which is
; the opposite of what an absent node means -- and it is reported rather than
; silently skipped.

; The decline is reported, not silent, but stdout and stderr interleave under
; -split-input-file so the warning cannot be ordered against the module text;
; what is pinned here is the observable result.
; CHECK-LABEL: llvm.func @callees_all_null
; CHECK-NOT: callees
define void @callees_all_null(ptr %fp) {
  call void %fp(), !callees !0
  ret void
}

!0 = !{null}
