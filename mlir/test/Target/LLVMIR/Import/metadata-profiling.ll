; RUN: mlir-translate -import-llvm -split-input-file %s | FileCheck %s

; CHECK-LABEL: @cond_br
define i64 @cond_br(i1 %arg1, i64 %arg2) {
entry:
  ; CHECK: llvm.cond_br
  ; CHECK-SAME: weights([0, 3])
  br i1 %arg1, label %bb1, label %bb2, !prof !0
bb1:
  ret i64 %arg2
bb2:
  ret i64 %arg2
}

!0 = !{!"branch_weights", i32 0, i32 3}

; // -----

; CHECK-LABEL: @cond_br_expected
define i64 @cond_br_expected(i1 %arg1, i64 %arg2) {
entry:
  ; CHECK: llvm.cond_br
  ; CHECK-SAME: weights([1, 2000])
  br i1 %arg1, label %bb1, label %bb2, !prof !0
bb1:
  ret i64 %arg2
bb2:
  ret i64 %arg2
}

!0 = !{!"branch_weights", !"expected", i32 1, i32 2000}

; // -----

; CHECK-LABEL: @simple_switch(
define i32 @simple_switch(i32 %arg1) {
  ; CHECK: llvm.switch
  ; CHECK: {branch_weights = array<i32: 42, 3, 5>}
  switch i32 %arg1, label %bbd [
    i32 0, label %bb1
    i32 9, label %bb2
  ], !prof !0
bb1:
  ret i32 %arg1
bb2:
  ret i32 %arg1
bbd:
  ret i32 %arg1
}

!0 = !{!"branch_weights", i32 42, i32 3, i32 5}

; // -----

; CHECK-LABEL: @simple_switch_expected(
define i32 @simple_switch_expected(i32 %arg1) {
  ; CHECK: llvm.switch
  ; CHECK: {branch_weights = array<i32: 1, 1, 2000>}
  switch i32 %arg1, label %bbd [
    i32 0, label %bb1
    i32 9, label %bb2
  ], !prof !0
bb1:
  ret i32 %arg1
bb2:
  ret i32 %arg1
bbd:
  ret i32 %arg1
}

!0 = !{!"branch_weights", !"expected", i32 1, i32 1, i32 2000}

; // -----

; Verify that a single weight attached to a call is not translated.
; The MLIR WeightedBranchOpInterface does not support this case.

; CHECK: llvm.func @fn()
declare i32 @fn()

; CHECK-LABEL: @call_branch_weights
define i32 @call_branch_weights() {
  ; CHECK:  llvm.call @fn() : () -> i32
  %1 = call i32 @fn(), !prof !0
  ret i32 %1
}

!0 = !{!"branch_weights", i32 42}

; // -----

declare void @foo()
declare i32 @__gxx_personality_v0(...)

; CHECK-LABEL: @invoke_branch_weights
define i32 @invoke_branch_weights() personality ptr @__gxx_personality_v0 {
  ; CHECK: llvm.invoke @foo() to ^bb2 unwind ^bb1 {branch_weights = array<i32: 42, 99>} : () -> ()
  invoke void @foo() to label %bb2 unwind label %bb1, !prof !0
bb1:
  %1 = landingpad { ptr, i32 } cleanup
  br label %bb2
bb2:
  ret i32 1

}

!0 = !{!"branch_weights", i32 42, i32 99}

; // -----

; CHECK-LABEL: @value_profile
define void @value_profile(ptr %f) {
  ; CHECK: llvm.call %{{.*}}() {llvm.value_profile = array<i64: 0, 1600, -7046029254386353131, 1500, 12345, 100>}
  call void %f(), !prof !0
  ret void
}

!0 = !{!"VP", i32 0, i64 1600, i64 -7046029254386353131, i64 1500, i64 12345, i64 100}
