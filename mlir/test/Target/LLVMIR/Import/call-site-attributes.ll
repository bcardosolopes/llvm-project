; RUN: mlir-translate -import-llvm %s | FileCheck %s
; They were dropped on the way back out too, so the round trip is the test.
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; `llvm.call` modelled a handful of call-site function attributes and dropped
; the rest. These are typed slots rather than a string bag so a pass can ask
; `op.getCold()` instead of string-matching, and so they do not round-trip
; through a re-parsed name. There is no op verifier behind them: the flags need
; none, and `alloc_size` is checked where it is written back out.

declare ptr @malloc(i64)
declare ptr @calloc(i64, i64)
declare i32 @__gxx_personality_v0(...)

; CHECK-LABEL: @on_a_call
define ptr @on_a_call(i64 %n) {
  ; The attribute dictionary prints in alphabetical order.
  ; `allocsize` is an integer attribute: the argument indices, not a flag.
  ; CHECK: llvm.call @malloc
  ; CHECK-SAME: alloc_size = array<i32: 0>
  ; CHECK-SAME: builtin
  ; CHECK-SAME: cold
  ; CHECK-SAME: min_size
  ; CHECK-SAME: no_merge
  ; CHECK-SAME: no_return
  ; CHECK-SAME: opt_size
  ; ROUNDTRIP: call ptr @malloc(i64 %{{.*}}) #[[CALL:[0-9]+]]
  %p = call ptr @malloc(i64 %n) #0
  ret ptr %p
}

; An invoke is a call with an unwind edge and takes the same attributes. It
; modelled only the calling convention before this.
; CHECK-LABEL: @on_an_invoke
define ptr @on_an_invoke(i64 %n, i64 %m) personality ptr @__gxx_personality_v0 {
  ; The same set as the call above, through the same shared helper, so the
  ; invoke path is covered for every slot rather than just two of them.
  ; CHECK: llvm.invoke @calloc
  ; CHECK-SAME: alloc_size = array<i32: 0, 1>
  ; CHECK-SAME: builtin
  ; CHECK-SAME: cold
  ; CHECK-SAME: min_size
  ; CHECK-SAME: no_merge
  ; CHECK-SAME: no_return
  ; CHECK-SAME: opt_size
  ; ROUNDTRIP: invoke ptr @calloc(i64 %{{.*}}, i64 %{{.*}}) #[[INV:[0-9]+]]
  %p = invoke ptr @calloc(i64 %n, i64 %m) #1 to label %ok unwind label %bad
ok:
  ret ptr %p
bad:
  %l = landingpad { ptr, i32 } cleanup
  ret ptr null
}

; A call with none of them gains none of them.
; CHECK-LABEL: @plain
define ptr @plain(i64 %n) {
  ; CHECK-NOT: opt_size
  ; CHECK-NOT: alloc_size
  %p = call ptr @malloc(i64 %n)
  ret ptr %p
}

attributes #0 = { builtin cold nomerge noreturn optsize minsize allocsize(0) }
attributes #1 = { builtin cold nomerge noreturn optsize minsize allocsize(0,1) }

; ROUNDTRIP-DAG: attributes #[[CALL]] = { builtin cold minsize nomerge noreturn optsize allocsize(0) }
; ROUNDTRIP-DAG: attributes #[[INV]] = { builtin cold minsize nomerge noreturn optsize allocsize(0,1) }
