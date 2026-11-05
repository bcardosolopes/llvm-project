; RUN: mlir-translate -import-llvm %s | FileCheck %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; The enum call-site attributes have typed slots. The string ones cannot: a
; frontend may attach any name it likes, so they ride in `passthrough`, the
; same bag `llvm.func` already uses.

declare ptr @malloc(i64)
declare ptr @calloc(i64, i64)
declare i32 @__gxx_personality_v0(...)

; CHECK-LABEL: @on_a_call
define ptr @on_a_call(i64 %n) {
  ; The modelled `cold` and `allocsize` keep their own slots and do not also
  ; show up here.
  ; CHECK: llvm.call @malloc
  ; CHECK-SAME: alloc_size = array<i32: 0>
  ; CHECK-SAME: cold
  ; A valueless string attribute is still written as a pair, with an empty
  ; value: a bare name means an *enum* attribute, and `"no-builtin-memcpy"`
  ; happens not to be one only by luck of spelling.
  ; CHECK-SAME: passthrough = [{{\[}}"no-builtin-memcpy", ""], ["probe-stack", "inline-asm"]]
  ; ROUNDTRIP: call ptr @malloc(i64 %{{.*}}) #[[CALL:[0-9]+]]
  %p = call ptr @malloc(i64 %n) #0
  ret ptr %p
}

; CHECK-LABEL: @on_an_invoke
define ptr @on_an_invoke(i64 %n, i64 %m) personality ptr @__gxx_personality_v0 {
  ; CHECK: llvm.invoke @calloc
  ; CHECK-SAME: passthrough = [{{\[}}"stack-probe-size", "8192"]]
  ; ROUNDTRIP: invoke ptr @calloc(i64 %{{.*}}, i64 %{{.*}}) #[[INV:[0-9]+]]
  %p = invoke ptr @calloc(i64 %n, i64 %m) #1 to label %ok unwind label %bad
ok:
  ret ptr %p
bad:
  %l = landingpad { ptr, i32 } cleanup
  ret ptr null
}

; A call whose every attribute has a slot gets no `passthrough` at all, rather
; than an empty array.
; CHECK-LABEL: @only_enum_attributes
define ptr @only_enum_attributes(i64 %n) {
  ; The dictionary prints in alphabetical order, so `passthrough` would come
  ; *after* `cold`. A `CHECK-NOT` between the two would only scan the gap
  ; between them and pass whatever happened; matching the whole line is what
  ; actually shows the attribute is absent.
  ; CHECK: llvm.call @malloc(%{{.*}}) {cold} : (i64) -> !llvm.ptr
  %p = call ptr @malloc(i64 %n) #2
  ret ptr %p
}

attributes #0 = { cold allocsize(0) "probe-stack"="inline-asm" "no-builtin-memcpy" }
attributes #1 = { "stack-probe-size"="8192" }
attributes #2 = { cold }

; ROUNDTRIP-DAG: attributes #[[CALL]] = { cold allocsize(0) "no-builtin-memcpy" "probe-stack"="inline-asm" }
; ROUNDTRIP-DAG: attributes #[[INV]] = { "stack-probe-size"="8192" }
