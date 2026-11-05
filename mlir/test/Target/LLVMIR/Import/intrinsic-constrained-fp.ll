; RUN: mlir-translate -import-llvm %s | FileCheck %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; The dialect had ops for four of the constrained intrinsics -- `fpext`,
; `fptrunc`, `sitofp`, `uitofp` -- so a module using any of the other
; forty-odd could not be imported at all: "unhandled intrinsic".
;
; One of each shape here, since the shapes are what the op definitions get
; right or wrong: the arity, which operand types are mangled into the name, and
; whether the operation takes a rounding mode at all.

declare double @llvm.experimental.constrained.fadd.f64(double, double, metadata, metadata)
declare double @llvm.experimental.constrained.fma.f64(double, double, double, metadata, metadata)
declare double @llvm.experimental.constrained.sqrt.f64(double, metadata, metadata)
declare double @llvm.experimental.constrained.powi.f64(double, i32, metadata, metadata)
declare double @llvm.experimental.constrained.ldexp.f64.i32(double, i32, metadata, metadata)
declare i32 @llvm.experimental.constrained.fptosi.i32.f64(double, metadata)
declare i32 @llvm.experimental.constrained.lrint.i32.f64(double, metadata, metadata)
declare i32 @llvm.experimental.constrained.lround.i32.f64(double, metadata)
declare double @llvm.experimental.constrained.maxnum.f64(double, double, metadata)
declare double @llvm.experimental.constrained.ceil.f64(double, metadata)

; CHECK-LABEL: @constrained
define void @constrained(double %a, double %b, double %c, i32 %i) strictfp {
  ; Binary, rounding mode, result type mangled.
  ; CHECK: llvm.intr.experimental.constrained.fadd
  ; ROUNDTRIP: call double @llvm.experimental.constrained.fadd.f64(double %{{.*}}, double %{{.*}}, metadata !"round.dynamic", metadata !"fpexcept.strict")
  %1 = call double @llvm.experimental.constrained.fadd.f64(double %a, double %b, metadata !"round.dynamic", metadata !"fpexcept.strict") strictfp
  ; Ternary.
  ; CHECK: llvm.intr.experimental.constrained.fma
  ; ROUNDTRIP: call double @llvm.experimental.constrained.fma.f64(double %{{.*}}, double %{{.*}}, double %{{.*}}, metadata !"round.tonearest", metadata !"fpexcept.ignore")
  %2 = call double @llvm.experimental.constrained.fma.f64(double %a, double %b, double %c, metadata !"round.tonearest", metadata !"fpexcept.ignore") strictfp
  ; Unary.
  ; CHECK: llvm.intr.experimental.constrained.sqrt
  ; ROUNDTRIP: call double @llvm.experimental.constrained.sqrt.f64(double %{{.*}}, metadata !"round.upward", metadata !"fpexcept.maytrap")
  %3 = call double @llvm.experimental.constrained.sqrt.f64(double %a, metadata !"round.upward", metadata !"fpexcept.maytrap") strictfp
  ; Second operand is a fixed i32, so it is *not* mangled into the name.
  ; CHECK: llvm.intr.experimental.constrained.powi
  ; ROUNDTRIP: call double @llvm.experimental.constrained.powi.f64(double %{{.*}}, i32 %{{.*}}, metadata !"round.downward", metadata !"fpexcept.strict")
  %4 = call double @llvm.experimental.constrained.powi.f64(double %a, i32 %i, metadata !"round.downward", metadata !"fpexcept.strict") strictfp
  ; Second operand is overloaded, so it *is* -- `.f64.i32`, unlike powi above.
  ; CHECK: llvm.intr.experimental.constrained.ldexp
  ; ROUNDTRIP: call double @llvm.experimental.constrained.ldexp.f64.i32(double %{{.*}}, i32 %{{.*}}, metadata !"round.dynamic", metadata !"fpexcept.strict")
  %5 = call double @llvm.experimental.constrained.ldexp.f64.i32(double %a, i32 %i, metadata !"round.dynamic", metadata !"fpexcept.strict") strictfp
  ; Float in, integer out, and exact -- no rounding mode operand.
  ; CHECK: llvm.intr.experimental.constrained.fptosi
  ; ROUNDTRIP: call i32 @llvm.experimental.constrained.fptosi.i32.f64(double %{{.*}}, metadata !"fpexcept.strict")
  %6 = call i32 @llvm.experimental.constrained.fptosi.i32.f64(double %a, metadata !"fpexcept.strict") strictfp
  ; Float in, integer out, and this one *does* round.
  ; CHECK: llvm.intr.experimental.constrained.lrint
  ; ROUNDTRIP: call i32 @llvm.experimental.constrained.lrint.i32.f64(double %{{.*}}, metadata !"round.dynamic", metadata !"fpexcept.strict")
  %7 = call i32 @llvm.experimental.constrained.lrint.i32.f64(double %a, metadata !"round.dynamic", metadata !"fpexcept.strict") strictfp
  ; ...and its sibling that does not, which is the pair most easily confused.
  ; CHECK: llvm.intr.experimental.constrained.lround
  ; ROUNDTRIP: call i32 @llvm.experimental.constrained.lround.i32.f64(double %{{.*}}, metadata !"fpexcept.strict")
  %8 = call i32 @llvm.experimental.constrained.lround.i32.f64(double %a, metadata !"fpexcept.strict") strictfp
  ; Binary and exact.
  ; CHECK: llvm.intr.experimental.constrained.maxnum
  ; ROUNDTRIP: call double @llvm.experimental.constrained.maxnum.f64(double %{{.*}}, double %{{.*}}, metadata !"fpexcept.strict")
  %9 = call double @llvm.experimental.constrained.maxnum.f64(double %a, double %b, metadata !"fpexcept.strict") strictfp
  ; Unary and exact.
  ; CHECK: llvm.intr.experimental.constrained.ceil
  ; ROUNDTRIP: call double @llvm.experimental.constrained.ceil.f64(double %{{.*}}, metadata !"fpexcept.strict")
  %10 = call double @llvm.experimental.constrained.ceil.f64(double %a, metadata !"fpexcept.strict") strictfp
  ret void
}
