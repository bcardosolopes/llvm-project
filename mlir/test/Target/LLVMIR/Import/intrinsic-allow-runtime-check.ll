; RUN: mlir-translate -import-llvm %s | FileCheck %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; `__builtin_allow_runtime_check`. The kind that names the check is an
; `MDString` operand rather than a value, and the importer had no path for one:
; `convertMetadataValue` only unwraps a `ValueAsMetadata`, so the generic
; argument conversion asserted, and was later turned into a decline. This
; models it instead, so the call survives the round trip.

declare i1 @llvm.allow.runtime.check(metadata)

; CHECK-LABEL: @allow_check
define i1 @allow_check() {
  ; The string becomes an attribute, since there is no value to hold it.
  ; CHECK: llvm.intr.allow.runtime.check "my_check" : i1
  ; ROUNDTRIP: call i1 @llvm.allow.runtime.check(metadata !"my_check")
  %c = call i1 @llvm.allow.runtime.check(metadata !"my_check")
  ret i1 %c
}

; Two calls with different kinds stay different: the attribute is the operand,
; not a marker that a check was here.
; CHECK-LABEL: @two_kinds
define i1 @two_kinds() {
  ; CHECK: llvm.intr.allow.runtime.check "check_a" : i1
  ; ROUNDTRIP: call i1 @llvm.allow.runtime.check(metadata !"check_a")
  %a = call i1 @llvm.allow.runtime.check(metadata !"check_a")
  ; CHECK: llvm.intr.allow.runtime.check "check_b" : i1
  ; ROUNDTRIP: call i1 @llvm.allow.runtime.check(metadata !"check_b")
  %b = call i1 @llvm.allow.runtime.check(metadata !"check_b")
  %r = and i1 %a, %b
  ret i1 %r
}
