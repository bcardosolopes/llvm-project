; RUN: mlir-translate -import-llvm -emit-expensive-warnings -split-input-file %s 2>&1 | FileCheck %s

; Three kinds a census of real IR turned up once the common ones were handled.

; CHECK-NOT: dropped metadata
; CHECK-NOT: unhandled

; !noalias on a prefetch. Unlike trap or assume, a prefetch does name the
; location it touches, so getAccessedOperands reports its address.
declare void @llvm.prefetch.p0(ptr, i32 immarg, i32 immarg, i32 immarg)

; CHECK: llvm.intr.prefetch
; CHECK-SAME: noalias_scopes = [#[[SCOPE:[a-z_0-9]+]]]
define void @prefetch_noalias(ptr %p) {
  call void @llvm.prefetch.p0(ptr %p, i32 0, i32 1, i32 1), !noalias !0
  ret void
}

!0 = !{!1}
!1 = distinct !{!1, !2, !"prefetch_noalias: %p"}
!2 = distinct !{!2, !"prefetch_noalias"}

; // -----

; !annotation on the *function* rather than on an instruction. LLVM permits the
; kind on a GlobalObject too, and one real library has 77 of them. The
; instruction path cannot carry these: convertDialectAttributes hands functions
; an empty instruction list.

; CHECK-NOT: unhandled function metadata
; CHECK: llvm.func @annotated_function() attributes {annotations = ["auto-init"]}
define void @annotated_function() !annotation !0 {
  ret void
}

!0 = !{!"auto-init"}

; // -----

; !irr_loop carries the profile weight of an irreducible loop header. LLVM reads
; it off the block's terminator and MachineBasicBlock carries it into block
; placement, so it is profile data that reaches codegen. The node's first
; operand is the fixed tag "loop_header_weight", so the weight alone is
; lossless.

; CHECK: llvm.cond_br %{{.*}}, ^bb1, ^bb2 {irr_loop_header_weight = 100 : i64}
define void @irreducible_header(i1 %c) {
entry:
  br label %hdr
hdr:
  br i1 %c, label %hdr, label %exit, !irr_loop !0
exit:
  ret void
}

!0 = !{!"loop_header_weight", i64 100}
