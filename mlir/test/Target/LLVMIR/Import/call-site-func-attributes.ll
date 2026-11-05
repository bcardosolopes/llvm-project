; RUN: mlir-translate -import-llvm %s | FileCheck %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; `llvm.func` already models these with typed slots, but on a call they went
; into the `passthrough` bag. The same LLVM attribute was a structured
; attribute on one carrier and a string in a list on the other --
; `target-features` worst of all, a queryable feature list against the raw
; "+neon,+sve". The call-site slots use the function's names and types.

declare void @callee()
declare i32 @__gxx_personality_v0(...)

; CHECK-LABEL: @on_a_call
define void @on_a_call() {
  ; Structured, not strings: `vscale_range` keeps its two fields, `uwtable`
  ; and `frame-pointer` their enums, `target-features` its parsed list.
  ; CHECK: llvm.call @callee()
  ; CHECK-SAME: frame_pointer = #llvm.framePointerKind<all>
  ; CHECK-SAME: no_inline
  ; CHECK-SAME: optimize_none
  ; CHECK-SAME: target_features = #llvm.target_features<["+neon", "+sve"]>
  ; CHECK-SAME: uwtable_kind = #llvm.uwtableKind<sync>
  ; CHECK-SAME: vscale_range = #llvm.vscale_range<minRange = 2 : i32, maxRange = 16 : i32>
  ; And nothing is left in the bag: every attribute here has a slot.
  ; CHECK-NOT: passthrough
  ; ROUNDTRIP: call void @callee() #[[CALL:[0-9]+]]
  call void @callee() #0
  ret void
}

; The same set on an invoke, through the same shared helper.
; CHECK-LABEL: @on_an_invoke
define void @on_an_invoke() personality ptr @__gxx_personality_v0 {
  ; CHECK: llvm.invoke @callee()
  ; CHECK-SAME: arm_in_za
  ; CHECK-SAME: target_features = #llvm.target_features<["+sve"]>
  ; ROUNDTRIP: invoke void @callee() #[[INV:[0-9]+]]
  invoke void @callee() #1 to label %ok unwind label %bad
ok:
  ret void
bad:
  %l = landingpad { ptr, i32 } cleanup
  ret void
}

; A *string* attribute spelled like an enum is still a different attribute, so
; it stays in the bag while the enum takes the slot. That distinction is why
; the bag is not simply keyed by name.
; CHECK-LABEL: @name_collision
define void @name_collision() {
  ; CHECK: llvm.call @callee()
  ; CHECK-SAME: no_return
  ; CHECK-SAME: passthrough = [{{\[}}"noreturn", ""]]
  ; ROUNDTRIP: call void @callee() #[[BOTH:[0-9]+]]
  call void @callee() #2
  ret void
}

attributes #0 = { noinline optnone uwtable(sync) vscale_range(2,16) "frame-pointer"="all" "target-features"="+neon,+sve" "target-cpu"="generic" "tune-cpu"="neoverse-v1" "unsafe-fp-math"="true" "no-infs-fp-math"="true" "no-nans-fp-math"="false" "approx-func-fp-math"="true" "no-signed-zeros-fp-math"="false" "denormal-fp-math"="ieee,ieee" "denormal-fp-math-f32"="preserve-sign,preserve-sign" "fp-contract"="fast" "instrument-function-entry"="__enter" "instrument-function-exit"="__exit" "aarch64_pstate_sm_enabled" "aarch64_new_za" }
attributes #1 = { noinline optnone uwtable(async) vscale_range(1,8) "target-features"="+sve" "aarch64_in_za" }
attributes #2 = { noreturn "noreturn" }

; Every one of them comes back, with the values it went in with.
; ROUNDTRIP-DAG: attributes #[[CALL]] = { noinline optnone uwtable(sync) vscale_range(2,16) "aarch64_new_za" "aarch64_pstate_sm_enabled" "approx-func-fp-math"="true" "denormal-fp-math"="ieee,ieee" "denormal-fp-math-f32"="preserve-sign,preserve-sign" "fp-contract"="fast" "frame-pointer"="all" "instrument-function-entry"="__enter" "instrument-function-exit"="__exit" "no-infs-fp-math"="true" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "target-cpu"="generic" "target-features"="+neon,+sve" "tune-cpu"="neoverse-v1" "unsafe-fp-math"="true" }
; ROUNDTRIP-DAG: attributes #[[INV]] = { noinline optnone uwtable vscale_range(1,8) "aarch64_in_za" "target-features"="+sve" }
; ROUNDTRIP-DAG: attributes #[[BOTH]] = { noreturn "noreturn" }
