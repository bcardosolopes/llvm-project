; RUN: mlir-translate -import-llvm -mlir-print-debuginfo %s | FileCheck %s \
; RUN:   --implicit-check-not="llvm.dbg.cu"

; The usual case, kept in its own file so the check that the attribute is
; absent covers the whole output rather than one section of a split.
;
; The subprogram already carries the compile unit, so nothing is orphaned and
; the module attribute stays off. That is what keeps this from appearing on the
; overwhelming majority of modules.

; CHECK-DAG: #[[CU:.+]] = #llvm.di_compile_unit<
; CHECK-DAG: #llvm.di_subprogram<{{.*}}compileUnit = #[[CU]]
; CHECK-DAG: llvm.func @reached_compile_unit
define void @reached_compile_unit() !dbg !3 {
  ret void
}

!llvm.dbg.cu = !{!1}
!llvm.module.flags = !{!0}
!0 = !{i32 2, !"Debug Info Version", i32 3}
!1 = distinct !DICompileUnit(language: DW_LANG_C99, file: !2, emissionKind: FullDebug)
!2 = !DIFile(filename: "t.c", directory: "/")
!3 = distinct !DISubprogram(name: "reached_compile_unit", scope: !1, file: !2, type: !4, spFlags: DISPFlagDefinition, unit: !1)
!4 = !DISubroutineType(types: !5)
!5 = !{null}
