; RUN: mlir-translate -import-llvm -convert-debug-rec-to-intrinsics %s | FileCheck %s
; RUN: mlir-translate -import-llvm -convert-debug-rec-to-intrinsics \
; RUN:   -emit-expensive-warnings %s 2>&1 | FileCheck %s --check-prefix=WARN

; When the address a `#dbg_assign` names is deleted, LLVM leaves an empty
; `MDNode` in its place, and the verifier names that a valid undef address.
; A link-time tool may convert records to intrinsics before importing, so that is the
; form a production link presents. No value converts from an empty node, so the
; assignment has to be dropped the way the record path drops it -- taking the
; whole module's import down instead loses every other function too.

; CHECK-LABEL: llvm.func @killed_address
; CHECK-NOT:   llvm.intr.dbg.assign
; CHECK:       llvm.return
; WARN: warning: dropped intrinsic: {{.*}}llvm.dbg.assign

define void @killed_address(i32 %v) !dbg !4 {
entry:
  %a = alloca i32, align 4, !DIAssignID !10
  #dbg_assign(i32 %v, !9, !DIExpression(), !10, !12, !DIExpression(), !11)
  store i32 %v, ptr %a, align 4, !DIAssignID !10
  ret void, !dbg !11
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, emissionKind: FullDebug)
!1 = !DIFile(filename: "a.c", directory: "/")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 7, !"debug-info-assignment-tracking", i1 true}
!4 = distinct !DISubprogram(name: "killed_address", file: !1, line: 1, type: !5, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!5 = !DISubroutineType(types: !6)
!6 = !{null}
!8 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!9 = !DILocalVariable(name: "a", scope: !4, file: !1, line: 2, type: !8)
!10 = distinct !DIAssignID()
!11 = !DILocation(line: 2, column: 3, scope: !4)
!12 = !{}
