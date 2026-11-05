; RUN: mlir-translate -import-llvm -emit-expensive-warnings -split-input-file %s 2>&1 | FileCheck %s \
; RUN:   --implicit-check-not="dropped function metadata: !dbg"
; RUN: mlir-translate -import-llvm -split-input-file %s 2>&1 | FileCheck %s --check-prefix=QUIET \
; RUN:   --implicit-check-not="dropped global metadata" \
; RUN:   --implicit-check-not="dropped function metadata"

; Metadata on a global or a function with no dialect model was skipped with a
; bare `continue`. Only *instruction* metadata ever warned, which is exactly why
; this class of loss went unnoticed: a whole-app census reported a clean bill on
; global-level metadata across all five apps while a probe showed six kinds
; being discarded outright, with no diagnostic on any of them.
;
; The kinds themselves are modelled by the change above this one. A custom kind
; is used here instead so the test keeps testing the diagnostic rather than
; going stale the moment another kind gains a model.

; CHECK: warning: dropped global metadata: !my.custom.kind on ptr @g
; QUIET-LABEL: llvm.mlir.global
@g = global i32 0, !my.custom.kind !0

!0 = !{!"x"}

; // -----

; CHECK: warning: dropped function metadata: !my.other.kind on ptr @f
; QUIET-LABEL: llvm.func @f
define void @f() !my.other.kind !0 {
  ret void
}

!0 = !{!"x"}

; // -----

; QUIET-LABEL: llvm.func @withdbg
define void @withdbg() !dbg !3 {
  ret void
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!5}
!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, emissionKind: FullDebug)
!1 = !DIFile(filename: "a.c", directory: "/")
!3 = distinct !DISubprogram(name: "withdbg", scope: !1, file: !1, line: 1, type: !4, unit: !0, spFlags: DISPFlagDefinition)
!4 = !DISubroutineType(types: !{null})
!5 = !{i32 2, !"Debug Info Version", i32 3}

; // -----

; CHECK: warning: unhandled global metadata: !0 = !{!"function_section_prefix", !"hot"} on ptr @oldtag
; QUIET: llvm.mlir.global external @oldtag(0 : i32) {addr_space = 0 : i32} : i32
@oldtag = global i32 0, !section_prefix !0

!0 = !{!"function_section_prefix", !"hot"}
