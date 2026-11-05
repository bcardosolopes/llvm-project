; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s

; A loop latch can be an invoke: when the last thing a loop body does is a call
; that can unwind, the normal destination is the branch back to the header. The
; importer knew br, cond_br and switch, so those loops lost their annotation
; even though nothing about the annotation itself was unsupported.

; CHECK-NOT: unhandled metadata

declare void @body()
declare i32 @__gxx_personality_v0(...)

; CHECK: #[[ANN:.+]] = #llvm.loop_annotation<mustProgress = true, id = distinct{{.*}}>
; CHECK: llvm.func @loop_with_invoke_latch
; CHECK: llvm.invoke @body() to ^bb1 unwind ^bb2 {loop_annotation = #[[ANN]]}
define void @loop_with_invoke_latch() personality ptr @__gxx_personality_v0 {
entry:
  br label %latch
latch:
  invoke void @body() to label %latch unwind label %lpad, !llvm.loop !0
lpad:
  %ex = landingpad { ptr, i32 } cleanup
  ret void
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.mustprogress"}
