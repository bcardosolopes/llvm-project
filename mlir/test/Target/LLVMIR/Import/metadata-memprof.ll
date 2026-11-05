; RUN: mlir-translate -import-llvm -split-input-file %s | FileCheck %s

; A single MemInfoBlock: one allocation call stack and one allocation-type tag.

; CHECK-LABEL: @simple_memprof
define ptr @simple_memprof() {
  ; CHECK: llvm.call @alloc({{.*}}) {llvm.callsite = array<i64: 123456789>, llvm.memprof = [#llvm.memprof_mib<call_stack = [123456789], tags = ["cold"]>]}
  %p = call ptr @alloc(i64 16), !memprof !0, !callsite !2
  ret ptr %p
}

declare ptr @alloc(i64)

!0 = !{!1}
!1 = !{!2, !"cold"}
!2 = !{i64 123456789}

; // -----

; Several MemInfoBlocks, a multi-frame call stack, and the trailing
; (context id, total size) pairs that a profile gathered with full context-size
; information carries.

; CHECK-LABEL: @rich_memprof
define ptr @rich_memprof(i64 %n) {
  ; CHECK: llvm.callsite = array<i64: 333>
  ; CHECK-SAME: llvm.memprof = [#llvm.memprof_mib<call_stack = [111, 222], tags = ["cold"], context_sizes = [#llvm.memprof_context_size<id = 999, size = 4096>]>, #llvm.memprof_mib<call_stack = [333], tags = ["notcold"]>]
  %p = call ptr @alloc(i64 %n), !memprof !0, !callsite !5
  ret ptr %p
}

declare ptr @alloc(i64)

!0 = !{!1, !4}
!1 = !{!2, !"cold", !3}
!2 = !{i64 111, i64 222}
!3 = !{i64 999, i64 4096}
!4 = !{!5, !"notcold"}
!5 = !{i64 333}

; // -----

; An invoke is a `CallBase` too, so it carries both kinds just as a call does.

; CHECK-LABEL: @memprof_on_invoke
define ptr @memprof_on_invoke() personality ptr @__gxx_personality_v0 {
  ; CHECK: llvm.invoke @alloc
  ; CHECK-SAME: llvm.callsite = array<i64: 7>, llvm.memprof = [#llvm.memprof_mib<call_stack = [7], tags = ["hot"]>]
  %p = invoke ptr @alloc(i64 16) to label %ok unwind label %bad, !memprof !0, !callsite !2
ok:
  ret ptr %p
bad:
  %l = landingpad { ptr, i32 } cleanup
  ret ptr null
}

declare ptr @alloc(i64)
declare i32 @__gxx_personality_v0(...)

!0 = !{!1}
!1 = !{!2, !"hot"}
!2 = !{i64 7}
