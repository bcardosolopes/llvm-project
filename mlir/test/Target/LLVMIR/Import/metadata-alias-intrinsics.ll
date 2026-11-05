; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s

; LLVM allows alias metadata on any call, including a call to an intrinsic the
; dialect *does* model. These seven had requiresAliasAnalysis=0, so there was
; nowhere to put it. They are the ones a census of real IR actually shows carrying
; !noalias -- an intrinsic sitting inside a noalias region picks up that
; region's metadata like any other instruction, even when the intrinsic itself
; names no memory.

; CHECK-NOT: unhandled metadata

declare void @llvm.experimental.noalias.scope.decl(metadata)
declare void @llvm.trap()
declare void @llvm.assume(i1)
declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_copy.p0(ptr, ptr)
declare void @llvm.va_end.p0(ptr)
declare ptr @llvm.stacksave.p0()
declare void @llvm.stackrestore.p0(ptr)

; CHECK-LABEL: llvm.func @carriers
; CHECK: llvm.intr.experimental.noalias.scope.decl {{.*}}noalias_scopes =
; CHECK: llvm.intr.assume {{.*}}noalias_scopes =
; CHECK: llvm.intr.vastart {{.*}}noalias_scopes =
; CHECK: llvm.intr.vacopy {{.*}}noalias_scopes =
; CHECK: llvm.intr.vaend {{.*}}noalias_scopes =
; CHECK: llvm.intr.stacksave {{.*}}noalias_scopes =
; CHECK: llvm.intr.stackrestore {{.*}}noalias_scopes =
; CHECK: llvm.intr.trap {noalias_scopes =
define void @carriers(i1 %c, ptr %a, ptr %b, ...) {
  call void @llvm.experimental.noalias.scope.decl(metadata !0), !noalias !0
  call void @llvm.assume(i1 %c), !noalias !0
  call void @llvm.va_start.p0(ptr %a), !noalias !0
  call void @llvm.va_copy.p0(ptr %a, ptr %b), !noalias !0
  call void @llvm.va_end.p0(ptr %a), !noalias !0
  %sp = call ptr @llvm.stacksave.p0(), !noalias !0
  call void @llvm.stackrestore.p0(ptr %sp), !noalias !0
  call void @llvm.trap(), !noalias !0
  ret void
}

!0 = !{!1}
!1 = distinct !{!1, !2, !"carriers: %p"}
!2 = distinct !{!2, !"carriers"}
