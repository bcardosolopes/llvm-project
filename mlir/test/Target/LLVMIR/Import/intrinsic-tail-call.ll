; RUN: mlir-translate -import-llvm %s | FileCheck %s
; The marker is lost on the way out too, so the round trip is the test.
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare float @llvm.sqrt.f32(float)
declare i32 @g(i32)

; `llvm.call` has a typed `TailCallKind` slot, but the intrinsic ops do not --
; and LLVM marks intrinsic calls `tail` routinely. 10,035 calls in one real
; library lost the marker, and with it the tail-call optimisation it licenses.

; CHECK-LABEL: @tail_intrinsics
define void @tail_intrinsics(ptr %d, ptr %s, float %x) {
  ; CHECK: llvm.intr.memcpy{{.*}}llvm.tail_call_kind = #llvm.tailcallkind<tail>
  ; ROUNDTRIP: tail call void @llvm.memcpy
  tail call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 16, i1 false)
  ; CHECK: llvm.intr.sqrt{{.*}}llvm.tail_call_kind = #llvm.tailcallkind<tail>
  ; ROUNDTRIP: tail call float @llvm.sqrt
  %r = tail call float @llvm.sqrt.f32(float %x)
  ret void
}

; An ordinary call keeps using its own typed slot -- it must not also pick up
; the discardable one, or the marker would be written twice.
; CHECK-LABEL: @ordinary_call
define i32 @ordinary_call(i32 %x) {
  ; CHECK: llvm.call tail @g
  ; CHECK-NOT: llvm.tail_call_kind
  ; ROUNDTRIP: tail call i32 @g
  %a = tail call i32 @g(i32 %x)
  ret i32 %a
}

; An unmarked intrinsic call gains nothing.
; CHECK-LABEL: @untailed
define float @untailed(float %x) {
  ; CHECK-NOT: llvm.tail_call_kind
  ; ROUNDTRIP-NOT: tail call float @llvm.sqrt
  %r = call float @llvm.sqrt.f32(float %x)
  ret float %r
}
