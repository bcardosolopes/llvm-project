; RUN: mlir-translate -import-llvm -emit-expensive-warnings -split-input-file %s 2>&1 | FileCheck %s

; A loop's `!llvm.loop` node carries the start and end source locations of the
; loop as bare DILocation operands, and clang emits them for every loop under
; -g. When the loop has been inlined those DILocations have an `inlinedAt`,
; which MLIR represents as a CallSiteLoc rather than a FusedLoc. Narrowing to
; FusedLoc turned both into null, and an annotation with nothing non-null left
; in it was discarded whole -- properties included.

; CHECK-NOT: unhandled metadata
; CHECK: #[[ANN:.+]] = #llvm.loop_annotation<disableNonforced = true, startLoc = {{.+}}, endLoc = {{.+}}, id = distinct{{.*}}>
; CHECK: llvm.func @inlined_loop
; CHECK: llvm.br ^bb1 {loop_annotation = #[[ANN]]}
define void @inlined_loop() !dbg !10 {
entry:
  br label %loop
loop:
  br label %loop, !dbg !12, !llvm.loop !20
}

!llvm.dbg.cu = !{!1}
!llvm.module.flags = !{!0}
!0 = !{i32 2, !"Debug Info Version", i32 3}
!1 = distinct !DICompileUnit(language: DW_LANG_C99, file: !2, producer: "clang", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug)
!2 = !DIFile(filename: "l.c", directory: "/")
!9 = !DISubroutineType(types: !{})
!10 = distinct !DISubprogram(name: "caller", scope: !2, file: !2, line: 1, type: !9, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !1)
!11 = distinct !DISubprogram(name: "inlinee", scope: !2, file: !2, line: 8, type: !9, scopeLine: 8, spFlags: DISPFlagDefinition, unit: !1)
!12 = !DILocation(line: 3, column: 3, scope: !11, inlinedAt: !14)
!13 = !DILocation(line: 5, column: 3, scope: !11, inlinedAt: !14)
!14 = !DILocation(line: 9, column: 1, scope: !10)
!20 = distinct !{!20, !12, !13, !21}
!21 = !{!"llvm.loop.disable_nonforced"}

; // -----

; The locations alone, with no properties at all, are still worth keeping: they
; are what makes an `!llvm.loop` node identify its loop.

; CHECK-NOT: unhandled metadata
; CHECK: #[[ANN:.+]] = #llvm.loop_annotation<startLoc = {{.+}}, endLoc = {{.+}}, id = distinct{{.*}}>
; CHECK: llvm.func @inlined_loop_locs_only
; CHECK: llvm.br ^bb1 {loop_annotation = #[[ANN]]}
define void @inlined_loop_locs_only() !dbg !10 {
entry:
  br label %loop
loop:
  br label %loop, !dbg !12, !llvm.loop !20
}

!llvm.dbg.cu = !{!1}
!llvm.module.flags = !{!0}
!0 = !{i32 2, !"Debug Info Version", i32 3}
!1 = distinct !DICompileUnit(language: DW_LANG_C99, file: !2, producer: "clang", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug)
!2 = !DIFile(filename: "l.c", directory: "/")
!9 = !DISubroutineType(types: !{})
!10 = distinct !DISubprogram(name: "caller", scope: !2, file: !2, line: 1, type: !9, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !1)
!11 = distinct !DISubprogram(name: "inlinee", scope: !2, file: !2, line: 8, type: !9, scopeLine: 8, spFlags: DISPFlagDefinition, unit: !1)
!12 = !DILocation(line: 3, column: 3, scope: !11, inlinedAt: !14)
!13 = !DILocation(line: 5, column: 3, scope: !11, inlinedAt: !14)
!14 = !DILocation(line: 9, column: 1, scope: !10)
!20 = distinct !{!20, !12, !13}
