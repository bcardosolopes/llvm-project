; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s

; !nonnull says the loaded pointer is not null. ValueTracking and
; PromoteMemoryToRegister both read it, so it is real optimization information
; rather than a hint. LLVM allows the kind only on a load -- a call says the
; same thing with a `nonnull` return attribute, which travels the
; parameter-attribute path instead.

; CHECK-NOT: dropped metadata

; CHECK-LABEL: llvm.func @nonnull_load
; CHECK: llvm.load %{{.*}} {alignment = 8 : i64, nonnull}
define ptr @nonnull_load(ptr %p) {
  %a = load ptr, ptr %p, !nonnull !0
  ret ptr %a
}

; An unmarked load must stay unmarked.
; CHECK-LABEL: llvm.func @plain_load
; CHECK: llvm.load
; CHECK-NOT: nonnull
define ptr @plain_load(ptr %p) {
  %a = load ptr, ptr %p
  ret ptr %a
}

!0 = !{}
