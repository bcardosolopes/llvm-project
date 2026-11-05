; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s

; A fence orders accesses rather than performing one, so it names no location --
; but it sits inside noalias regions like any other instruction, and real IR
; attaches !noalias to it. It was the last carrier a census of real IR reported
; for the kind.

; CHECK-NOT: unhandled metadata

; CHECK-LABEL: llvm.func @fenced
; CHECK: llvm.fence seq_cst {noalias_scopes = [#[[SCOPE:[a-z_0-9]+]]]}
define void @fenced() {
  fence seq_cst, !noalias !0
  ret void
}

!0 = !{!1}
!1 = distinct !{!1, !2, !"fenced: %p"}
!2 = distinct !{!2, !"fenced"}
