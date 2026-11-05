; RUN: mlir-translate -import-llvm -mlir-print-debuginfo %s | FileCheck %s
; A link-time tool may convert records to intrinsics before importing, so the same
; module reaches the importer both ways and both have to hold the link.
; RUN: mlir-translate -import-llvm -convert-debug-rec-to-intrinsics \
; RUN:   -mlir-print-debuginfo %s | FileCheck %s
; The link between the store and the record is the point, and it only shows in
; the exported IR, so the round trip is the test.
; RUN: mlir-translate -import-llvm -mlir-print-debuginfo %s \
; RUN:   | mlir-translate -mlir-to-llvmir | FileCheck %s --check-prefix=ROUNDTRIP

; `#dbg_assign` is the assignment-tracking form of `#dbg_value`: it names the
; value assigned *and* the address assigned to, plus the `!DIAssignID` shared
; with the store that performs the assignment. The importer had no MLIR form
; for it and dropped every one -- silently, as the drop only warns under
; expensive warnings -- so a module built with assignment tracking, which is
; what Clang emits at `-O1` and above, came back describing no assignments.

define void @assign(i32 %v) !dbg !4 {
entry:
  ; The record and the store carry one identity, and it has to be the same one
  ; on the way out or the store is no longer described by the record.
  ; CHECK: llvm.alloca
  ; CHECK-SAME: llvm.dbg_assign_id = #llvm.di_assign_id<id = distinct[[[ID:[0-9]+]]]<>>
  %a = alloca i32, align 4, !DIAssignID !10
  ; CHECK: llvm.intr.dbg.assign
  ; CHECK-SAME: id = #llvm.di_assign_id<id = distinct[[[ID]]]<>>
    #dbg_assign(i32 %v, !9, !DIExpression(), !10, ptr %a, !DIExpression(), !11)
  ; CHECK: llvm.store
  ; CHECK-SAME: llvm.dbg_assign_id = #llvm.di_assign_id<id = distinct[[[ID]]]<>>
  store i32 %v, ptr %a, align 4, !DIAssignID !10
  ret void, !dbg !11
}

; ROUNDTRIP: %[[A:.*]] = alloca i32, align 4, !DIAssignID ![[ID:[0-9]+]]
; ROUNDTRIP: #dbg_assign(i32 %{{.*}}, ![[VAR:[0-9]+]], !DIExpression(), ![[ID]], ptr %[[A]], !DIExpression(), !{{[0-9]+}})
; ROUNDTRIP: store i32 %{{.*}}, ptr %[[A]], align 4, !DIAssignID ![[ID]]
; ROUNDTRIP-DAG: ![[ID]] = distinct !DIAssignID()
; ROUNDTRIP-DAG: ![[VAR]] = !DILocalVariable(name: "a"

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, emissionKind: FullDebug)
!1 = !DIFile(filename: "a.c", directory: "/")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 7, !"debug-info-assignment-tracking", i1 true}
!4 = distinct !DISubprogram(name: "assign", file: !1, line: 1, type: !5, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!5 = !DISubroutineType(types: !6)
!6 = !{null}
!8 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!9 = !DILocalVariable(name: "a", scope: !4, file: !1, line: 2, type: !8)
!10 = distinct !DIAssignID()
!11 = !DILocation(line: 2, column: 3, scope: !4)
