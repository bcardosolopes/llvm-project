; RUN: mlir-translate -import-llvm -split-input-file %s | FileCheck %s

; CHECK-LABEL: @assertions
define ptr @assertions(ptr %p, float %x, ptr %q) {
  ; CHECK: llvm.load
  ; CHECK-SAME: llvm.align_metadata = 16 : i64
  ; CHECK-SAME: llvm.noalias_addrspace = [#llvm.constant_range<i32, 1, 2>, #llvm.constant_range<i32, 5, 6>]
  %l = load ptr, ptr %p, !align !0, !noalias.addrspace !2
  ; CHECK: llvm.fadd
  ; CHECK-SAME: llvm.fpmath = 2.500000e+00 : f32
  %a = fadd float %x, %x, !fpmath !1
  ; CHECK: llvm.store
  ; CHECK-SAME: llvm.noalias_addrspace = [#llvm.constant_range<i32, 0, 3>]
  store ptr %l, ptr %q, !noalias.addrspace !3
  ret ptr %l
}

!0 = !{i64 16}
!1 = !{float 2.500000e+00}
; Two disjoint ranges: address space 1 and address space 5, NOT 1 through 5.
!2 = !{i32 1, i32 2, i32 5, i32 6}
!3 = !{i32 0, i32 3}
