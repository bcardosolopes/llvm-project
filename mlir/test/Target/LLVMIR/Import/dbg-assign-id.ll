; RUN: mlir-translate -import-llvm %s | FileCheck %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; `!DIAssignID` is how assignment tracking links a store to the `#dbg_assign`
; record describing it: both name the same node. The node carries nothing, so
; identity is the entire content -- two attachments that shared a node have to
; come back sharing one, and two that did not must stay apart.

define void @two_identities() !dbg !4 {
entry:
  ; CHECK: llvm.alloca
  ; CHECK-SAME: llvm.dbg_assign_id = #llvm.di_assign_id<id = distinct[[[A:[0-9]+]]]<>>
  %a = alloca i32, align 4, !DIAssignID !10
  ; CHECK: llvm.store
  ; CHECK-SAME: llvm.dbg_assign_id = #llvm.di_assign_id<id = distinct[[[B:[0-9]+]]]<>>
  store i32 1, ptr %a, align 4, !DIAssignID !12
  ret void, !dbg !11
}

; Both attachments name one node, which is the case the whole mechanism rests
; on: a fresh distinct node per attachment would silently unlink the store from
; its record.
define void @one_identity() !dbg !13 {
entry:
  ; CHECK: llvm.alloca
  ; CHECK-SAME: llvm.dbg_assign_id = #llvm.di_assign_id<id = distinct[[[S:[0-9]+]]]<>>
  %a = alloca i32, align 4, !DIAssignID !14
  ; CHECK: llvm.store
  ; CHECK-SAME: llvm.dbg_assign_id = #llvm.di_assign_id<id = distinct[[[S]]]<>>
  store i32 1, ptr %a, align 4, !DIAssignID !14
  ret void, !dbg !15
}

; The bodies come before the metadata definitions in the exported module, so
; the instruction checks are grouped here in order and the node definitions
; follow. Two `-DAG` lines with the same pattern must match two *different*
; lines, so this also pins `X` and `Y` apart.
; ROUNDTRIP: alloca i32, align 4, !DIAssignID ![[X:[0-9]+]]
; ROUNDTRIP: store i32 1, ptr %{{.*}}, align 4, !DIAssignID ![[Y:[0-9]+]]
; ROUNDTRIP: alloca i32, align 4, !DIAssignID ![[SHARED:[0-9]+]]
; ROUNDTRIP: store i32 1, ptr %{{.*}}, align 4, !DIAssignID ![[SHARED]]
; ROUNDTRIP-DAG: ![[X]] = distinct !DIAssignID()
; ROUNDTRIP-DAG: ![[Y]] = distinct !DIAssignID()
; ROUNDTRIP-DAG: ![[SHARED]] = distinct !DIAssignID()

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, emissionKind: FullDebug)
!1 = !DIFile(filename: "a.c", directory: "/")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{i32 7, !"debug-info-assignment-tracking", i1 true}
!4 = distinct !DISubprogram(name: "two_identities", file: !1, line: 1, type: !5, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!5 = !DISubroutineType(types: !6)
!6 = !{null}
!10 = distinct !DIAssignID()
!11 = !DILocation(line: 2, column: 3, scope: !4)
!12 = distinct !DIAssignID()
!13 = distinct !DISubprogram(name: "one_identity", file: !1, line: 10, type: !5, scopeLine: 10, spFlags: DISPFlagDefinition, unit: !0)
!14 = distinct !DIAssignID()
!15 = !DILocation(line: 11, column: 3, scope: !13)
