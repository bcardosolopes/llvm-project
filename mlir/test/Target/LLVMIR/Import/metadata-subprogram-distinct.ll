; RUN: mlir-translate -import-llvm -mlir-print-debuginfo -mlir-print-local-scope %s \
; RUN:   | FileCheck %s
; The bug was on the way back out, and LLVM's own verifier is what catches it,
; so the round trip is the test: -mlir-to-llvmir fails the module if the
; subprogram is written uniqued.
; RUN: mlir-translate -import-llvm -mlir-print-debuginfo %s \
; RUN:   | mlir-translate -mlir-to-llvmir | FileCheck %s --check-prefix=ROUNDTRIP

; A `distinct !DISubprogram` that is *not* a definition. LLVM allows this, and
; attaches it to a function definition here. Distinctness was decided on the
; `Definition` flag alone when writing back out, so this came out uniqued and
; LLVM refused the module:
;
;   function definition may only have a distinct !dbg attachment
;
; The importer records distinctness in `id`, so a non-definition that was
; distinct keeps an `id` and stays distinct on the way out.

; The subprogram has to be the one attached to `@f1`, not merely a distinct
; subprogram named "bar" sitting somewhere in the module -- which is all the
; capture proved before, because nothing referenced it.
;
; `-mlir-print-local-scope` turns off attribute aliases, so the subprogram
; prints inline in the function's location and the attachment is one match
; rather than a chain through `#loc` and `#di_subprogram` definitions whose
; print order is an implementation detail.
; `CHECK-LABEL` marks where `@f1` starts, so the location below is looked for
; after it rather than anywhere in the output. It is not a hard fence -- label
; blocks are delimited by further matches of the same pattern, and there is
; only one function here -- so if this file ever grows a second one, the two
; want separate labels. Not `CHECK-NEXT` off the terminator either: that would
; pin how the printer lays the two lines out, which is not what is being
; tested.
; CHECK-LABEL: llvm.func @f1
; CHECK: } loc(fused<#llvm.di_subprogram<id = distinct{{.*}}name = "bar"

; ROUNDTRIP: define <{ i32, i32 }> @f1() !dbg ![[SP:[0-9]+]]
; ROUNDTRIP: ![[SP]] = distinct !DISubprogram(name: "bar"

define <{i32, i32}> @f1() !dbg !0 {
  ret <{i32, i32}> zeroinitializer
}

!llvm.dbg.cu = !{!5}
!llvm.module.flags = !{!6}

!0 = distinct !DISubprogram(name: "bar", linkageName: "_ZN3foo3barEv", line: 3, isDefinition: false, scopeLine: 3, file: !4, scope: !1, type: !2)
!1 = !DIFile(filename: "/foo", directory: "bar.cpp")
!2 = !DISubroutineType(types: !3)
!3 = !{null}
!4 = !DIFile(filename: "/foo", directory: "bar.cpp")
!5 = distinct !DICompileUnit(language: DW_LANG_C99, isOptimized: true, emissionKind: FullDebug, file: !4)
!6 = !{i32 1, !"Debug Info Version", i32 3}
