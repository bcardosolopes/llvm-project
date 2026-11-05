; RUN: mlir-translate -import-llvm %s | FileCheck %s

; An MLIR op cannot pass its own result to a successor, so the import puts a
; block between an invoke and a normal destination that takes the invoke's
; result, and tags its branch so an exporter's consumer can fold it back.

declare i32 @may_throw()
declare i32 @__gxx_personality_v0(...)

; CHECK-LABEL: llvm.func @invoke_result_phi
define i32 @invoke_result_phi() personality ptr @__gxx_personality_v0 {
entry:
  %r = invoke i32 @may_throw() to label %join unwind label %pad
; CHECK: llvm.invoke @may_throw() to ^[[FWD:.*]] unwind
; CHECK: ^[[FWD]]:
; CHECK-NEXT: llvm.br ^{{.*}}(%{{.*}} : i32) {llvm_forwarding_branch}
join:
  %p = phi i32 [ %r, %entry ]
  ret i32 %p
pad:
  %lp = landingpad { ptr, i32 } cleanup
  ret i32 0
}
