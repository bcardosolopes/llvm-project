; RUN: mlir-translate -import-llvm %s | FileCheck %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; `llvm.call` modelled these six flags and the memory effects; `llvm.invoke`
; modelled none of them and dropped all seven. An invoke is a call with an
; unwind edge, so both now read them through one shared helper and cannot
; drift apart again.

declare ptr @calloc(i64, i64)
declare i32 @__gxx_personality_v0(...)

; CHECK-LABEL: @on_an_invoke
define ptr @on_an_invoke(i64 %n, i64 %m) personality ptr @__gxx_personality_v0 {
  ; CHECK: llvm.invoke @calloc
  ; CHECK-SAME: convergent
  ; CHECK-SAME: inline_hint
  ; CHECK-SAME: memory_effects = #llvm.memory_effects<other = none, argMem = read, inaccessibleMem = none, errnoMem = none, targetMem0 = none, targetMem1 = none>
  ; CHECK-SAME: no_inline
  ; `nounwind` on an invoke is legal: it makes the unwind edge unreachable
  ; rather than contradicting it.
  ; CHECK-SAME: no_unwind
  ; CHECK-SAME: will_return
  ; ROUNDTRIP: invoke ptr @calloc(i64 %{{.*}}, i64 %{{.*}}) #[[INV:[0-9]+]]
  %p = invoke ptr @calloc(i64 %n, i64 %m) #0 to label %ok unwind label %bad
ok:
  ret ptr %p
bad:
  %l = landingpad { ptr, i32 } cleanup
  ret ptr null
}

; An invoke with none of them gains none of them. `memory_effects` in
; particular is only recorded when it differs from the read-write default.
; CHECK-LABEL: @plain_invoke
define ptr @plain_invoke(i64 %n, i64 %m) personality ptr @__gxx_personality_v0 {
  ; CHECK: llvm.invoke @calloc
  ; CHECK-NOT: memory_effects
  ; CHECK-NOT: convergent
  %p = invoke ptr @calloc(i64 %n, i64 %m) to label %ok unwind label %bad
ok:
  ret ptr %p
bad:
  %l = landingpad { ptr, i32 } cleanup
  ret ptr null
}

attributes #0 = { convergent nounwind willreturn noinline inlinehint memory(argmem: read) }

; ROUNDTRIP: attributes #[[INV]] = { convergent inlinehint noinline nounwind willreturn memory(argmem: read) }
