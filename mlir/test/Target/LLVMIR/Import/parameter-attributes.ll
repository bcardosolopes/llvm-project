; RUN: mlir-translate -import-llvm -split-input-file %s | FileCheck %s

; The parameter attributes that used to have no entry in the AttrKind table and
; were therefore dropped on the way in. `dead_on_unwind` and `writable` are the
; two that matter most in practice: clang puts both on every `sret` return, so
; every function returning a non-trivial class by value carried them.

; CHECK-LABEL: llvm.func @sret_return
; CHECK-SAME: %{{.*}}: !llvm.ptr {llvm.align = 8 : i64, llvm.dead_on_unwind, llvm.noalias, llvm.sret = !llvm.struct<"struct.Big", (f64)>, llvm.writable}
%struct.Big = type { double }
define void @sret_return(ptr dead_on_unwind noalias writable sret(%struct.Big) align 8 %agg.result) {
  ret void
}

; // -----

; `captures` is the modern replacement for `nocapture`, and LLVM 21 removed the
; old kind altogether -- `ptr nocapture` now parses as `captures(none)`. The
; dialect has not switched to the bitmask representation yet, so the importer
; maps `captures(none)` back to the `llvm.nocapture` unit attribute and there is
; no separate spelling to test.

; CHECK-LABEL: llvm.func @captures_none
; CHECK-SAME: %{{.*}}: !llvm.ptr {llvm.nocapture}
define void @captures_none(ptr captures(none) %p) {
  ret void
}

; // -----

; CHECK-LABEL: llvm.func @nofpclass_param
; CHECK-SAME: %{{.*}}: f32 {llvm.nofpclass = {{[0-9]+}} : i64}
define float @nofpclass_param(float nofpclass(nan inf) %x) {
  ret float %x
}

; // -----

; `initializes` is a list of disjoint ranges rather than a single one, so it
; imports as an array of the same range attribute the scalar kinds use.

; CHECK-LABEL: llvm.func @initializes_param
; CHECK-SAME: %{{.*}}: !llvm.ptr {llvm.initializes = [#llvm.constant_range<i64, 0, 4>, #llvm.constant_range<i64, 8, 12>]}
define void @initializes_param(ptr initializes((0, 4), (8, 12)) %p) {
  ret void
}

; // -----

; CHECK-LABEL: llvm.func @noext_param
; CHECK-SAME: %{{.*}}: i32 {llvm.noext}
define void @noext_param(i32 noext %x) {
  ret void
}

; // -----

; CHECK-LABEL: llvm.func @swift_params
; CHECK-SAME: %{{.*}}: !llvm.ptr {llvm.swiftself}
define void @swift_params(ptr swiftself %a) {
  ret void
}

; // -----

; CHECK-LABEL: llvm.func swiftcc @swifterror_param
; CHECK-SAME: %{{.*}}: !llvm.ptr {llvm.swifterror}
define swiftcc void @swifterror_param(ptr swifterror %a) {
  ret void
}

; // -----

; CHECK-LABEL: llvm.func swifttailcc @swiftasync_param
; CHECK-SAME: %{{.*}}: !llvm.ptr {llvm.swiftasync}
define swifttailcc void @swiftasync_param(ptr swiftasync %a) {
  ret void
}

; // -----

; The return position takes them too, for the kinds LLVM allows there.

; CHECK-LABEL: llvm.func @noext_result
; CHECK-SAME: -> (i32 {llvm.noext})
define noext i32 @noext_result() {
  ret i32 0
}

; // -----

; CHECK-LABEL: llvm.func @nofpclass_result
; CHECK-SAME: -> (f32 {llvm.nofpclass = {{[0-9]+}} : i64})
define nofpclass(nan) float @nofpclass_result() {
  ret float 0.0
}
