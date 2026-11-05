; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s

; A `!llvm.loop` node is a loop's identity, not just a bag of options: LLVM
; builds it `distinct` and passes use it to tell one loop from another and to
; record that a transformation has run. Two loops that happen to carry the same
; options are still two loops.
;
; `LoopAnnotationAttr` had no identity, so those two uniqued to one attribute
; and came back out sharing one node -- at which point a transformation
; recorded on one reads as recorded on the other.

define void @two_loops_same_options(i1 %c) {
entry:
  br label %l1
l1:
  ; CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !llvm.loop ![[L1:[0-9]+]]
  br i1 %c, label %l1, label %l2, !llvm.loop !0
l2:
  ; The second loop must not reuse the first one's node.
  ; CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !llvm.loop ![[L2:[0-9]+]]
  br i1 %c, label %l2, label %done, !llvm.loop !2
done:
  ret void
}

; Sharing is still right when it is the *same* loop: every latch of one loop
; carries one node, so the identity is per node, not per branch.
define void @one_loop_two_latches(i1 %c) {
entry:
  br label %head
head:
  br i1 %c, label %latchA, label %latchB
latchA:
  ; CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !llvm.loop ![[SHARED:[0-9]+]]
  br i1 %c, label %head, label %done, !llvm.loop !3
latchB:
  ; CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !llvm.loop ![[SHARED]]
  br i1 %c, label %head, label %done, !llvm.loop !3
done:
  ret void
}

; Each is self-referential and distinct, and the two from the first function
; are different nodes. Two `-DAG` lines with the same pattern have to match two
; different lines, which is what pins them apart.
; CHECK-DAG: ![[L1]] = distinct !{![[L1]], !{{[0-9]+}}}
; CHECK-DAG: ![[L2]] = distinct !{![[L2]], !{{[0-9]+}}}
; CHECK-DAG: ![[SHARED]] = distinct !{![[SHARED]], !{{[0-9]+}}}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.unroll.disable"}
!2 = distinct !{!2, !1}
!3 = distinct !{!3, !1}
