; RUN: mlir-translate -import-llvm %s | FileCheck %s
; It is lost on the way back out too, so the round trip is the test.
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP
; The module itself now mentions the attribute, so this arm sends the IR to
; /dev/null and checks only the diagnostics.
; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=QUIET --allow-empty

; `!ro_after_init` is the marker a read-only-after-init pass writes to record
; that it has already run on a module -- `checkAndInsertMetadata` returns early
; when it is present. Dropping it makes a processed module look unprocessed, so
; the pass runs a second time. It is a correctness marker, not a hint.

; CHECK: module attributes {
; CHECK-SAME: llvm.ro_after_init
; ROUNDTRIP: !ro_after_init = !{![[EMPTY:[0-9]+]]}
; ROUNDTRIP: ![[EMPTY]] = !{}
; QUIET-NOT: ro_after_init

@g = global i32 0

!ro_after_init = !{!0}
!0 = !{}
