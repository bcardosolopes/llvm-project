; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s

; Inline asm that touches memory carries the same alias metadata a call does.
; llvm.inline_asm was not a memory-access op, so it had nowhere to keep it and
; every attachment was dropped. The reproducer from a census of real IR is a stack
; pointer read in jni_zero.

; CHECK-NOT: unhandled metadata

; CHECK: llvm.inline_asm
; CHECK-SAME: alias_scopes = [#[[SCOPE:[a-z_0-9]+]]]
; CHECK-SAME: noalias_scopes = [#[[NOALIAS:[a-z_0-9]+]]]
; CHECK-SAME: srcloc = array<i64: 77>
define i64 @sp() {
  %r = tail call i64 asm sideeffect "mov $0, sp", "=r"(), !noalias !0, !alias.scope !3, !srcloc !6
  ret i64 %r
}

!0 = !{!1}
!1 = distinct !{!1, !2, !"sp: %a"}
!2 = distinct !{!2, !"sp"}
!3 = !{!4}
!4 = distinct !{!4, !2, !"sp: %b"}
!6 = !{i64 77}
