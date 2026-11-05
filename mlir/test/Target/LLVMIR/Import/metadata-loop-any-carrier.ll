; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s \
; RUN:   --implicit-check-not="dropped metadata" \
; RUN:   --implicit-check-not="unhandled metadata"

; LLVM permits `!llvm.loop` on *any* instruction, not only on the four
; terminators the dialect models with a typed `loop_annotation` slot. Real IR
; relies on that: SimplifyCFG turning an invoke latch into a call leaves the
; node behind on a non-terminator, and the app sweep found ~340 such sites
; across several real apps.
;
; Anything without a typed slot keeps the same LoopAnnotationAttr as a
; discardable dialect attribute, so no carrier can silently drop it.

declare void @body()

; CHECK-LABEL: llvm.func @many_carriers
define void @many_carriers(ptr %p, i1 %c) {
entry:
  br label %hdr
hdr:
  ; CHECK: llvm.load %{{.*}} {alignment = 4 : i64, llvm.loop_annotation = #[[ANN:.+]]}
  %v = load i32, ptr %p, !llvm.loop !0
  ; CHECK: llvm.store %{{.*}} {alignment = 4 : i64, llvm.loop_annotation = #[[ANN]]}
  store i32 %v, ptr %p, !llvm.loop !0
  ; CHECK: llvm.add %{{.*}} {llvm.loop_annotation = #[[ANN]]}
  %a = add i32 %v, 1, !llvm.loop !0
  ; CHECK: llvm.call @body() {llvm.loop_annotation = #[[ANN]]}
  call void @body(), !llvm.loop !0
  ; A terminator still takes the typed slot rather than the fallback.
  ; CHECK: llvm.cond_br %{{.*}}, ^bb1, ^bb2 {loop_annotation = #[[ANN]]}
  br i1 %c, label %hdr, label %exit, !llvm.loop !0
exit:
  ret void
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.mustprogress"}
