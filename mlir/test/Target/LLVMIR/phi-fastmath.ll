; A phi of floating-point type keeps its fast-math flags through the round
; trip. A block argument has none of its own, so every edge into the block
; records them on its terminator (`llvm.phi_fastmath`), a unit attribute
; standing for a successor whose phis have none.

; RUN: mlir-translate -import-llvm %s | FileCheck --check-prefix=IMPORT %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s

; IMPORT-LABEL: llvm.func @select_like(
; IMPORT: llvm.cond_br %{{.*}}, ^bb1, ^bb2({{.*}}) {llvm.phi_fastmath = [unit, [#llvm.fastmath<fast>, #llvm.fastmath<nsz>, #llvm.fastmath<none>]]}
; IMPORT: llvm.br ^bb2({{.*}}) {llvm.phi_fastmath = {{\[}}[#llvm.fastmath<fast>, #llvm.fastmath<nsz>, #llvm.fastmath<none>]]}
; CHECK-LABEL: define float @select_like(
; CHECK: phi fast float
; CHECK: phi nsz <2 x double>
; CHECK: phi i32
define float @select_like(i1 %c, float %a, float %b, <2 x double> %v, ptr %p) {
entry:
  br i1 %c, label %then, label %join
then:
  %m = fmul fast float %a, %b
  br label %join
join:
  %r = phi fast float [ %m, %then ], [ %a, %entry ]
  %w = phi nsz <2 x double> [ zeroinitializer, %then ], [ %v, %entry ]
  %i = phi i32 [ 1, %then ], [ 0, %entry ]
  store <2 x double> %w, ptr %p
  store i32 %i, ptr %p
  ret float %r
}

; Phis without flags record nothing.
; IMPORT-LABEL: llvm.func @no_flags(
; IMPORT-NOT: llvm.phi_fastmath
; IMPORT: llvm.return
define double @no_flags(i1 %c, double %a) {
entry:
  br i1 %c, label %then, label %join
then:
  br label %join
join:
  %r = phi double [ 1.0, %then ], [ %a, %entry ]
  ret double %r
}
