; RUN: mlir-translate -import-llvm %s | FileCheck %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; The enum and integer call-site attributes with no typed slot. LLVM has
; roughly fifty function attributes; the handful a pass actually asks about
; have slots, and the rest ride in `passthrough` rather than being dropped.

declare ptr @malloc(i64)
declare ptr @calloc(i64, i64)
declare i32 @__gxx_personality_v0(...)

; CHECK-LABEL: @on_a_call
define ptr @on_a_call(i64 %n) {
  ; `cold` has a slot, so it stays there and is *not* also in the bag: the same
  ; fact must not be reachable two ways.
  ; CHECK: llvm.call @malloc
  ; CHECK-SAME: cold
  ; An integer attribute keeps its value as an integer, so it stays distinct
  ; from a string attribute that happens to share the name.
  ; CHECK-SAME: passthrough = ["nobuiltin", "nofree", "norecurse", "nosync", "returns_twice", "strictfp", ["alignstack", 16], ["probe-stack", "inline-asm"]]
  ; ROUNDTRIP: call ptr @malloc(i64 %{{.*}}) #[[CALL:[0-9]+]]
  %p = call ptr @malloc(i64 %n) #0
  ret ptr %p
}

; CHECK-LABEL: @on_an_invoke
define ptr @on_an_invoke(i64 %n, i64 %m) personality ptr @__gxx_personality_v0 {
  ; CHECK: llvm.invoke @calloc
  ; CHECK-SAME: passthrough = ["nobuiltin"]
  ; ROUNDTRIP: invoke ptr @calloc(i64 %{{.*}}, i64 %{{.*}}) #[[INV:[0-9]+]]
  %p = invoke ptr @calloc(i64 %n, i64 %m) #1 to label %ok unwind label %bad
ok:
  ret ptr %p
bad:
  %l = landingpad { ptr, i32 } cleanup
  ret ptr null
}

; A *string* attribute may be spelled like an enum one. They are different
; attributes and both have to survive: the enum in its slot, the string in the
; bag as a pair, which is what tells the two apart on the way out.
; CHECK-LABEL: @name_collision
define ptr @name_collision(i64 %n) {
  ; CHECK: llvm.call @malloc
  ; CHECK-SAME: no_return
  ; CHECK-SAME: passthrough = [{{\[}}"noreturn", ""]]
  ; ROUNDTRIP: call ptr @malloc(i64 %{{.*}}) #[[BOTH:[0-9]+]]
  %p = call ptr @malloc(i64 %n) #3
  ret ptr %p
}

; Everything here has a slot, so the bag is absent rather than empty.
; CHECK-LABEL: @all_modelled
define ptr @all_modelled(i64 %n) {
  ; CHECK: llvm.call @malloc
  ; CHECK-NOT: passthrough
  ; CHECK-SAME: cold
  %p = call ptr @malloc(i64 %n) #2
  ret ptr %p
}

attributes #0 = { cold nobuiltin returns_twice strictfp nosync nofree norecurse alignstack=16 "probe-stack"="inline-asm" }
attributes #3 = { noreturn "noreturn" }
attributes #1 = { nobuiltin }
attributes #2 = { cold noreturn optsize }

; ROUNDTRIP-DAG: attributes #[[CALL]] = { cold nobuiltin nofree norecurse nosync returns_twice strictfp alignstack=16 "probe-stack"="inline-asm" }
; ROUNDTRIP-DAG: attributes #[[INV]] = { nobuiltin }
; ROUNDTRIP-DAG: attributes #[[BOTH]] = { noreturn "noreturn" }
