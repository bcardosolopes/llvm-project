; RUN: mlir-translate -import-llvm -mlir-print-debuginfo %s -o %t.mlir
; RUN: FileCheck %s < %t.mlir
; RUN: mlir-translate -mlir-to-llvmir %t.mlir | FileCheck %s --check-prefix=ROUNDTRIP

; `!llvm.preserve.access.index` is the record type a BPF CO-RE relocation is
; taken against. It is kept as the DIType rather than by name: the name would
; go stale under renaming, and an anonymous record has none.

declare ptr @llvm.preserve.struct.access.index.p0.p0(ptr, i32, i32)

; CHECK-DAG: #[[TYA:.+]] = #llvm.di_composite_type<{{.*}}name = "sockptr_t"
define ptr @access(ptr %p) !dbg !6 {
  ; CHECK-DAG: llvm.preserve_access_index = #[[TYA]]
  ; ROUNDTRIP: !llvm.preserve.access.index ![[TY:[0-9]+]]
  %r = call ptr @llvm.preserve.struct.access.index.p0.p0(ptr elementtype(i64) %p, i32 1, i32 1), !dbg !9, !llvm.preserve.access.index !4
  ret ptr %r
}

!llvm.dbg.cu = !{!2}
!llvm.module.flags = !{!0, !1}
!0 = !{i32 2, !"Debug Info Version", i32 3}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = distinct !DICompileUnit(language: DW_LANG_C99, file: !3, emissionKind: FullDebug)
!3 = !DIFile(filename: "t.c", directory: "/")
!4 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "sockptr_t", file: !3, line: 2, size: 128, elements: !5)
!5 = !{}
!6 = distinct !DISubprogram(name: "access", scope: !3, file: !3, line: 4, type: !7, spFlags: DISPFlagDefinition, unit: !2)
!7 = !DISubroutineType(types: !8)
!8 = !{null}
!9 = !DILocation(line: 5, column: 3, scope: !6)

; ROUNDTRIP: ![[TY]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "sockptr_t"
