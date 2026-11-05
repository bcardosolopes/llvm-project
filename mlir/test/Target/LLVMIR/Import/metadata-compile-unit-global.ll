; RUN: mlir-translate -import-llvm -mlir-print-debuginfo %s | FileCheck %s \
; RUN:   --implicit-check-not="llvm.dbg.cu"

; Reached through a *global* rather than a subprogram, in its own file so the
; absence check covers the whole output.
;
; This is the case `op->getAttrs()` alone misses: a global's `dbg_exprs` is an
; inherent attribute held in properties, not in the attribute dictionary, so
; the walk has to look there too. Without that the unit reads as unreachable
; and the module picks up an `llvm.dbg.cu` attribute naming something that is
; already pointed at.

; CHECK: llvm.mlir.global external @g
@g = global i32 0, align 4, !dbg !0

!llvm.dbg.cu = !{!2}
!llvm.module.flags = !{!6}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(name: "g", scope: !2, file: !3, line: 1, type: !5, isLocal: false, isDefinition: true)
!2 = distinct !DICompileUnit(language: DW_LANG_C99, file: !3, emissionKind: FullDebug, globals: !4)
!3 = !DIFile(filename: "g.c", directory: "/")
!4 = !{!0}
!5 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!6 = !{i32 2, !"Debug Info Version", i32 3}
