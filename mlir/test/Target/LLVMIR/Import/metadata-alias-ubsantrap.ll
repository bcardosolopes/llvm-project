; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s

; The last !noalias carrier the app sweep reports: @llvm.ubsantrap, 27 sites
; across 4 containers of one app. Like @llvm.trap it names no location, but the
; check it guards sits inside a noalias region, so real IR attaches the region's
; !noalias to it.
;
; The !nosanitize here is deliberate: a ubsantrap carries both in practice, and
; the two travel different paths -- one an op attribute, one a discardable
; dialect attribute -- so this pins that they coexist.

; CHECK-NOT: dropped metadata
; CHECK-NOT: unhandled metadata

declare void @llvm.ubsantrap(i8 immarg)

; CHECK-LABEL: llvm.func @checked
; CHECK: llvm.intr.ubsantrap <{failureKind = 2 : i8, noalias_scopes = [#[[SCOPE:[a-z_0-9]+]]]}> {llvm.nosanitize}
define void @checked() {
  call void @llvm.ubsantrap(i8 2), !noalias !0, !nosanitize !3
  ret void
}

!0 = !{!1}
!1 = distinct !{!1, !2, !"checked: %p"}
!2 = distinct !{!2, !"checked"}
!3 = !{}
