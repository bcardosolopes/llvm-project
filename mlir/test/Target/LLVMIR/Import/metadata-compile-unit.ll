; RUN: mlir-translate -import-llvm -mlir-print-debuginfo -split-input-file %s | FileCheck %s

; A compile unit that no subprogram points at. It still carries the enums,
; retained types and global variables the backend emits, so it is recorded on
; the module rather than dropped.

; CHECK-DAG: #[[CU:.+]] = #llvm.di_compile_unit<{{.*}}sourceLanguage = DW_LANG_C_plus_plus_14{{.*}}producer = "clang"{{.*}}>
; CHECK-DAG: llvm.dbg.cu = [#[[CU]]]
; CHECK-DAG: llvm.func @orphan_compile_unit
define ptr @orphan_compile_unit(i64 %size) {
  %ptr = call ptr @alloc(i64 %size)
  ret ptr %ptr
}

declare ptr @alloc(i64)

!llvm.dbg.cu = !{!1}
!llvm.module.flags = !{!0}
!0 = !{i32 2, !"Debug Info Version", i32 3}
!1 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !2, producer: "clang", emissionKind: FullDebug)
!2 = !DIFile(filename: "alloc.cpp", directory: "/")

; // -----

; Two units, one reached through the subprogram and one not. Only the orphan is
; recorded: the reached one already arrives that way, and listing it here as
; well would export it twice.

; CHECK-DAG: #[[OFILE:.+]] = #llvm.di_file<"orphan.c" in "/">
; CHECK-DAG: #[[ORPHAN:.+]] = #llvm.di_compile_unit<{{.*}}file = #[[OFILE]]{{.*}}>
; CHECK-DAG: llvm.dbg.cu = [#[[ORPHAN]]]
; CHECK-DAG: llvm.func @one_of_two
define void @one_of_two() !dbg !3 {
  ret void
}

!llvm.dbg.cu = !{!1, !6}
!llvm.module.flags = !{!0}
!0 = !{i32 2, !"Debug Info Version", i32 3}
!1 = distinct !DICompileUnit(language: DW_LANG_C99, file: !2, emissionKind: FullDebug)
!2 = !DIFile(filename: "reached.c", directory: "/")
!3 = distinct !DISubprogram(name: "one_of_two", scope: !1, file: !2, type: !4, spFlags: DISPFlagDefinition, unit: !1)
!4 = !DISubroutineType(types: !5)
!5 = !{null}
!6 = distinct !DICompileUnit(language: DW_LANG_C99, file: !7, emissionKind: FullDebug)
!7 = !DIFile(filename: "orphan.c", directory: "/")
