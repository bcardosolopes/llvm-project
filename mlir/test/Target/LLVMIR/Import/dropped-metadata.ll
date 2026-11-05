; RUN: mlir-translate -import-llvm -emit-expensive-warnings -split-input-file %s 2>&1 | FileCheck %s
; RUN: mlir-translate -import-llvm -split-input-file %s 2>&1 | FileCheck %s --check-prefix=QUIET --implicit-check-not="dropped metadata"

; Metadata kinds with no LLVM dialect representation are skipped. Warn about
; them under -emit-expensive-warnings so the loss is not silent.
;
; A custom kind is used rather than a real one so the test keeps testing the
; diagnostic rather than going stale the moment the kind it named gains a model
; -- which is exactly what happened to the `!memprof` and `!callsite` this case
; used to be written against.

; CHECK: warning: dropped metadata: !my.custom.kind on
; QUIET-LABEL: @dropped_custom_kind
define ptr @dropped_custom_kind() {
  %p = call ptr @alloc(i64 16), !my.custom.kind !0
  ret ptr %p
}

declare ptr @alloc(i64)

!0 = !{!"x"}

; // -----

; Load and store flags are imported by the operation builders rather than the
; dialect interface, so they must not be reported as dropped.

; QUIET-LABEL: @op_builder_metadata_not_reported
define i32 @op_builder_metadata_not_reported(ptr %p) {
  ; CHECK-NOT: dropped metadata: !invariant.load
  ; CHECK-NOT: dropped metadata: !nontemporal
  %v = load i32, ptr %p, !invariant.load !0, !nontemporal !0
  ret i32 %v
}

!0 = !{}
