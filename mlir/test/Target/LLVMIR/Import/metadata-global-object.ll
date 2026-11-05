; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s \
; RUN:   --implicit-check-not="dropped global metadata" \
; RUN:   --implicit-check-not="dropped function metadata"

; The GlobalObject-level kinds. LLVM permits each of these on a global variable
; or a function, so both carriers model them rather than guessing which occurs.

; CHECK: llvm.mlir.global external @g(0 : i32)
; CHECK-SAME: absolute_symbol = #llvm.constant_range<i64, 0, 256>
; CHECK-SAME: exclude
@g = global i32 0, !absolute_symbol !0, !exclude !1

; `!associated` becomes a symbol reference rather than a copied name, for the
; same reason `!callees` does: a string would go stale under
; NamespaceSymbolsPass.
; CHECK: llvm.mlir.global external @h(1 : i32)
; CHECK-SAME: associated = @g
@h = global i32 1, !associated !2

; CHECK: llvm.mlir.global external @vt()
; CHECK-SAME: vcall_visibility = 1 : i64
@vt = global [1 x ptr] zeroinitializer, !vcall_visibility !3

; CHECK: llvm.mlir.global external @cold(2 : i32)
; CHECK-SAME: section_prefix = "unlikely"
@cold = global i32 2, !section_prefix !6

; CHECK: llvm.func @f() attributes
; CHECK-SAME: kcfi_type = 12345 : i32
; CHECK-SAME: section_prefix = "hot"
define void @f() !kcfi_type !5 !section_prefix !4 {
  ret void
}

; CHECK: llvm.func @f2() attributes
; CHECK-SAME: section_prefix = "hot"
define void @f2() !section_prefix !7 {
  ret void
}

!0 = !{i64 0, i64 256}
!1 = !{}
!2 = !{ptr @g}
!3 = !{i64 1}
!4 = !{!"function_section_prefix", !"hot"}
!5 = !{i32 12345}
!6 = !{!"section_prefix", !"unlikely"}
!7 = !{!"section_prefix", !"hot"}
