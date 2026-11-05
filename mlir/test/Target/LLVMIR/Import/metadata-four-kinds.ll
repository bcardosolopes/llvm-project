; RUN: mlir-translate -import-llvm %s | FileCheck %s
; The tags print as attribute aliases, so the two are pinned once here and
; referred to by their alias names below.
; CHECK: #mmra_tag = #llvm.mmra_tag<"amdgpu-as":"global">
; CHECK: #mmra_tag1 = #llvm.mmra_tag<"amdgpu-as":"local">
; Each of these was dropped in both directions, so the round trip is the test.
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; The two sanitizer prologue words. `!func_sanitize` lives on the function, so
; it is a typed slot rather than a discardable attribute -- a function is handed
; an empty instruction list, so the instruction path would never write it back.
; CHECK-LABEL: llvm.func @valueless_and_mmra
; CHECK-SAME: func_sanitize = array<i32: -1056584962, 42>
; ROUNDTRIP-LABEL: @valueless_and_mmra
; ROUNDTRIP-SAME: !func_sanitize ![[FS:[0-9]+]]
define i32 @valueless_and_mmra(ptr %p, i1 %c) !func_sanitize !4 {
entry:
  ; CHECK: llvm.alloca {{.*}}llvm.coro_outside_frame
  ; ROUNDTRIP: alloca i32, align 4, !coro.outside.frame ![[EMPTY:[0-9]+]]
  %a = alloca i32, !coro.outside.frame !0
  ; CHECK: llvm.cond_br {{.*}}llvm.make_implicit
  ; ROUNDTRIP: br i1 %{{.*}}, !make.implicit ![[EMPTY]]
  br i1 %c, label %t, label %e, !make.implicit !0
t:
  ; A single tag is a bare pair, not a tuple of one.
  ; CHECK: llvm.load {{.*}}llvm.mmra = #mmra_tag}
  ; ROUNDTRIP: load i32, ptr %{{.*}}, !mmra ![[ONE:[0-9]+]]
  %v = load i32, ptr %p, !mmra !1
  ; CHECK: llvm.fence {{.*}}llvm.mmra = [#mmra_tag, #mmra_tag1]
  ; ROUNDTRIP: fence release, !mmra ![[MANY:[0-9]+]]
  fence release, !mmra !2
  ret i32 %v
e:
  ret i32 0
}

; `!make.implicit` is not a branch-only kind, so the writer does not filter by
; carrier: `GuardUtils` reads it off a guard call, `Local`'s switch-to-branch
; rewrite off a switch, and `CodeGenPrepare` off a select. Pin one of those so
; narrowing the writer to terminators would be red rather than silent.
declare void @sink()

; Plain directives, not labelled ones: FileCheck scopes captured variables to a
; label block, and `EMPTY` is captured above.
; CHECK: llvm.func @make_implicit_on_a_call
; ROUNDTRIP: @make_implicit_on_a_call
define void @make_implicit_on_a_call() {
  ; CHECK: llvm.call @sink() {{.*}}llvm.make_implicit
  ; ROUNDTRIP: call void @sink(), !make.implicit ![[EMPTY]]
  call void @sink(), !make.implicit !0
  ret void
}

!0 = !{}
!1 = !{!"amdgpu-as", !"global"}
!2 = !{!1, !3}
!3 = !{!"amdgpu-as", !"local"}
!4 = !{i32 3238382334, i32 42}

; ROUNDTRIP-DAG: ![[EMPTY]] = !{}
; ROUNDTRIP-DAG: ![[ONE]] = !{!"amdgpu-as", !"global"}
; ROUNDTRIP-DAG: ![[MANY]] = !{![[ONE]], ![[LOCAL:[0-9]+]]}
; ROUNDTRIP-DAG: ![[LOCAL]] = !{!"amdgpu-as", !"local"}
; ROUNDTRIP-DAG: ![[FS]] = !{i32 -1056584962, i32 42}
