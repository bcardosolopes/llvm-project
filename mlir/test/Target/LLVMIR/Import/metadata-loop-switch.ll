; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s

; A loop latch is usually a conditional branch, but it can be a switch -- a
; do/while around a switch-based state machine produces one. `!llvm.loop` was
; declined for those loops purely because llvm.switch had nowhere to keep it,
; and the whole annotation was lost.

declare void @a()
declare void @b()

; The alias name is derived from the attribute mnemonic and is stable, so it is
; matched literally rather than captured -- a capture defined outside the
; CHECK-LABEL block is not visible inside it.
; CHECK-NOT: unhandled metadata
; CHECK: #loop_annotation = #llvm.loop_annotation<mustProgress = true, id = distinct{{.*}}>
; CHECK-LABEL: @switch_latch
define void @switch_latch(i32 %n) {
entry:
  br label %latch
latch:
  %i = phi i32 [ 0, %entry ], [ %next, %c0 ], [ %next, %c1 ]
  %next = add i32 %i, 1
  ; A switch prints across several lines, so the attribute is not on the same
  ; line as the op and CHECK-SAME would never match.
  ; CHECK: llvm.switch
  ; CHECK: loop_annotation = #loop_annotation
  switch i32 %i, label %exit [ i32 0, label %c0
                               i32 1, label %c1 ], !llvm.loop !0
c0:
  call void @a()
  br label %latch
c1:
  call void @b()
  br label %latch
exit:
  ret void
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.mustprogress"}
