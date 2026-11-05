; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s

; !unpredictable marks a branch that is not worth predicting. It is valueless --
; its presence is the whole content -- and CodeGenPrepare and SelectOptimize
; read it when choosing between a branch and a conditional move, so dropping it
; changes codegen.

; CHECK-NOT: dropped metadata
; CHECK-NOT: unhandled metadata

declare void @a()

; CHECK-LABEL: llvm.func @carriers
define i32 @carriers(i1 %c, i32 %x, i32 %y) {
  ; CHECK: llvm.cond_br %{{.*}}, ^bb1, ^bb2 {unpredictable}
  br i1 %c, label %t, label %f, !unpredictable !0
t:
  ; CHECK: llvm.select %{{.*}} {unpredictable}
  %s = select i1 %c, i32 %x, i32 %y, !unpredictable !0
  ret i32 %s
f:
  ret i32 0
}

; CHECK-LABEL: llvm.func @switch_carrier
define void @switch_carrier(i32 %v) {
  ; CHECK: llvm.switch %{{.*}} : i32, ^bb1 [
  ; CHECK: ] {unpredictable}
  switch i32 %v, label %d [ i32 0, label %d ], !unpredictable !0
d:
  ret void
}

; An unmarked branch must stay unmarked.
; CHECK-LABEL: llvm.func @not_marked
; CHECK: llvm.cond_br
; CHECK-NOT: unpredictable
define void @not_marked(i1 %c) {
  br i1 %c, label %t, label %f
t:
  ret void
f:
  ret void
}

!0 = !{}
