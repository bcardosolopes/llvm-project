; RUN: mlir-translate -import-llvm %s | FileCheck %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; The two constrained comparisons, which the rest of the family left behind:
; their first metadata operand is a comparison predicate rather than a rounding
; mode, so they need a predicate attribute the others have no use for.
;
; `fcmp` is quiet and `fcmps` signals on a NaN operand. They are separate
; intrinsics and must not be folded together -- the difference is observable.

declare i1 @llvm.experimental.constrained.fcmp.f64(double, double, metadata, metadata)
declare i1 @llvm.experimental.constrained.fcmps.f64(double, double, metadata, metadata)

; CHECK-LABEL: @compares
; CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0.000000e+00 : f64) : f64
define void @compares(double %a, double %b) strictfp {
  ; CHECK: llvm.intr.experimental.constrained.fcmp oeq, %{{.*}}, %{{.*}} strict {llvm.call_fn_attrs = ["strictfp"]} : f64 to i1
  ; ROUNDTRIP: call i1 @llvm.experimental.constrained.fcmp.f64(double %{{.*}}, double %{{.*}}, metadata !"oeq", metadata !"fpexcept.strict")
  %1 = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"oeq", metadata !"fpexcept.strict") strictfp
  ; A different predicate, so it must not collapse onto the one above.
  ; CHECK: llvm.intr.experimental.constrained.fcmp ult, %{{.*}}, %{{.*}} ignore {llvm.call_fn_attrs = ["strictfp"]} : f64 to i1
  ; ROUNDTRIP: call i1 @llvm.experimental.constrained.fcmp.f64(double %{{.*}}, double %{{.*}}, metadata !"ult", metadata !"fpexcept.ignore")
  %2 = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"ult", metadata !"fpexcept.ignore") strictfp
  ; The signalling form stays the signalling form.
  ; CHECK: llvm.intr.experimental.constrained.fcmps oeq, %{{.*}}, %{{.*}} strict {llvm.call_fn_attrs = ["strictfp"]} : f64 to i1
  ; ROUNDTRIP: call i1 @llvm.experimental.constrained.fcmps.f64(double %{{.*}}, double %{{.*}}, metadata !"oeq", metadata !"fpexcept.strict")
  %3 = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"oeq", metadata !"fpexcept.strict") strictfp
  ; CHECK: llvm.intr.experimental.constrained.fcmp oeq, %{{.*}}, %[[ZERO]] strict {llvm.call_fn_attrs = ["strictfp"]} : f64 to i1
  ; ROUNDTRIP: call i1 @llvm.experimental.constrained.fcmp.f64(double %{{.*}}, double 0.000000e+00, metadata !"oeq", metadata !"fpexcept.strict")
  %4 = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double 0.0, metadata !"oeq", metadata !"fpexcept.strict") strictfp
  ret void
}
