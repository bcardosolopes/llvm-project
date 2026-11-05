; RUN: mlir-translate -import-llvm %s | FileCheck %s
; What matters is that the exported node is the loop's *own* node, which only
; the round trip can show.
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; `!llvm.mem.parallel_loop_access` is the legacy form of `!llvm.access.group`,
; but `Loop::isAnnotatedParallel` still falls back to it, and that check is
; all-or-nothing: one memory operation in the loop carrying neither and the
; whole loop stops counting as parallel. Dropping it on import therefore did
; not lose an annotation, it lost the parallelism.

define void @one_loop(ptr %p, i1 %c) {
entry:
  br label %body
body:
  ; CHECK: llvm.load
  ; CHECK-SAME: llvm.mem_parallel_loop_access = [#[[LOOP:.+]]]
  %v = load i32, ptr %p, align 4, !llvm.mem.parallel_loop_access !0
  ; CHECK: llvm.store
  ; CHECK-SAME: llvm.mem_parallel_loop_access = [#[[LOOP]]]
  store i32 %v, ptr %p, align 4, !llvm.mem.parallel_loop_access !0
  ; The same annotation the memory operations name, so the export can hand
  ; back the loop's own node rather than an equal copy.
  ; CHECK: llvm.cond_br
  ; CHECK-SAME: loop_annotation = #[[LOOP]]
  br i1 %c, label %body, label %done, !llvm.loop !0
done:
  ret void
}

; The reference has to be the loop's own node, not an equal copy of it:
; `isAnnotatedParallel` tests `is_contained(operands, loopID)` by identity.
; ROUNDTRIP: load i32, ptr %{{.*}}, align 4, !llvm.mem.parallel_loop_access ![[ID:[0-9]+]]
; ROUNDTRIP: store i32 %{{.*}}, ptr %{{.*}}, align 4, !llvm.mem.parallel_loop_access ![[ID]]
; ROUNDTRIP: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !llvm.loop ![[ID]]
; ROUNDTRIP: ![[ID]] = distinct !{![[ID]], !{{[0-9]+}}}

; Nested parallel loops: the operand is a list of loop identifiers rather than
; one self-referential node, and both are kept.
define void @nested_loops(ptr %p, i1 %c) {
entry:
  br label %outer
outer:
  br label %inner
inner:
  ; CHECK: llvm.load
  ; CHECK-SAME: llvm.mem_parallel_loop_access = [#[[A:.+]], #[[B:.+]]]
  %v = load i32, ptr %p, align 4, !llvm.mem.parallel_loop_access !4
  ; Inner first, then outer, matching the order the list names them in.
  ; CHECK: llvm.cond_br
  ; CHECK-SAME: loop_annotation = #[[A]]
  br i1 %c, label %inner, label %outer_latch, !llvm.loop !2
outer_latch:
  ; CHECK: llvm.cond_br
  ; CHECK-SAME: loop_annotation = #[[B]]
  br i1 %c, label %outer, label %done, !llvm.loop !3
done:
  ret void
}

!0 = distinct !{!0, !1}
!1 = !{!"llvm.loop.unroll.disable"}
!2 = distinct !{!2, !1}
!3 = distinct !{!3, !1}
!4 = !{!2, !3}
