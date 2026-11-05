; RUN: mlir-translate -import-llvm -split-input-file %s | FileCheck %s

; !heapallocsite names the type of the object an allocating call returns. Clang
; attaches it to every `operator new` when debug info is on.

; CHECK: #[[TY:.+]] = #llvm.di_composite_type<tag = DW_TAG_structure_type, name = "S"
; CHECK: llvm.func @allocate
; CHECK: llvm.call @_Znwm(%{{.*}}) {heapallocsite = #[[TY]]}
define ptr @allocate(i64 %size) {
  %ptr = call ptr @_Znwm(i64 %size), !heapallocsite !6
  ret ptr %ptr
}

declare ptr @_Znwm(i64)

!llvm.dbg.cu = !{!1}
!llvm.module.flags = !{!0}

!0 = !{i32 2, !"Debug Info Version", i32 3}
!1 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !2, producer: "clang", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug)
!2 = !DIFile(filename: "alloc.cpp", directory: "/")
!6 = !DICompositeType(tag: DW_TAG_structure_type, name: "S", file: !2, line: 1, size: 32, elements: !7)
!7 = !{}

; // -----

; An allocation whose type clang could not name carries an empty node rather
; than no metadata at all -- CodeViewDebug reads it with a dyn_cast<DIType>, so
; the empty node still marks the call as an allocation site. di_null_type is
; what keeps that distinct from the attribute being absent.

; CHECK: #[[NULLTY:.+]] = #llvm.di_null_type
; CHECK: llvm.func @allocate_void
; CHECK: llvm.call @malloc(%{{.*}}) {heapallocsite = #[[NULLTY]]}
define ptr @allocate_void(i64 %size) {
  %ptr = call ptr @malloc(i64 %size), !heapallocsite !3
  ret ptr %ptr
}

declare ptr @malloc(i64)

!llvm.module.flags = !{!0}

!0 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !{}

; // -----

; An allocating call inside a try block is an invoke, and carries the same
; metadata.

; CHECK: #[[TY:.+]] = #llvm.di_composite_type<tag = DW_TAG_structure_type, name = "S"
; CHECK: llvm.func @allocate_invoke
; CHECK: llvm.invoke @_Znwm(%{{.*}}) to ^bb1 unwind ^bb2 {heapallocsite = #[[TY]]}
define ptr @allocate_invoke(i64 %size) personality ptr @__gxx_personality_v0 {
entry:
  %ptr = invoke ptr @_Znwm(i64 %size) to label %done unwind label %lpad, !heapallocsite !6

done:
  ret ptr %ptr

lpad:
  %ex = landingpad { ptr, i32 } cleanup
  ret ptr null
}

declare ptr @_Znwm(i64)
declare i32 @__gxx_personality_v0(...)

!llvm.dbg.cu = !{!1}
!llvm.module.flags = !{!0}

!0 = !{i32 2, !"Debug Info Version", i32 3}
!1 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !2, producer: "clang", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug)
!2 = !DIFile(filename: "alloc.cpp", directory: "/")
!6 = !DICompositeType(tag: DW_TAG_structure_type, name: "S", file: !2, line: 1, size: 32, elements: !7)
!7 = !{}
