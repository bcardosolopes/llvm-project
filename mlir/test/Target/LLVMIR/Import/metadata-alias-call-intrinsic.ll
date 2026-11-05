; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s

; An intrinsic the dialect does not model imports as llvm.call_intrinsic. That
; op was not a memory-access op, so it had nowhere to keep alias metadata and
; every attachment was dropped. On Android this is not an edge case:
; @llvm.load.relative is how every relative-vtable virtual call loads its
; target, and after LTO those loads carry !noalias.

; CHECK-NOT: unhandled metadata
; CHECK: llvm.call_intrinsic "llvm.load.relative.i32"
; CHECK-SAME: alias_scopes = [#[[SCOPE:[a-z_0-9]+]]]
; CHECK-SAME: noalias_scopes = [#[[NOALIAS:[a-z_0-9]+]]]
; CHECK-SAME: tbaa = [#[[TBAA:[a-z_0-9]+]]]
declare ptr @llvm.load.relative.i32(ptr, i32)

define ptr @vcall(ptr %vtable) {
  %fn = call ptr @llvm.load.relative.i32(ptr %vtable, i32 8), !alias.scope !3, !noalias !0, !tbaa !6
  ret ptr %fn
}

!0 = !{!1}
!1 = distinct !{!1, !2, !"vcall: %vtable"}
!2 = distinct !{!2, !"vcall"}
!3 = !{!4}
!4 = distinct !{!4, !2, !"vcall: %fn"}
!6 = !{!7, !7, i64 0}
!7 = !{!"any pointer", !8, i64 0}
!8 = !{!"omnipotent char", !9, i64 0}
!9 = !{!"Simple C++ TBAA"}
