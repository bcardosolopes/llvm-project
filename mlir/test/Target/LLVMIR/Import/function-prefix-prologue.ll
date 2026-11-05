; RUN: mlir-translate -import-llvm %s | FileCheck %s
; Both were dropped in each direction, so the round trip is the test.
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; Prefix and prologue data are constants LLVM emits immediately before a
; function's entry point -- Swift uses prefix data for type metadata and
; -fsanitize=function uses prologue data -- so losing them changes the object,
; not just an optimization hint.

; CHECK: llvm.func @with_prefix
; CHECK-SAME: prefix_data = 42 : i32
; ROUNDTRIP: define i64 @with_prefix() prefix i32 42
define i64 @with_prefix() prefix i32 42 {
  ret i64 0
}

; CHECK: llvm.func @with_prologue
; CHECK-SAME: prologue_data = 99 : i32
; ROUNDTRIP: define i64 @with_prologue() prologue i32 99
define i64 @with_prologue() prologue i32 99 {
  ret i64 0
}

; CHECK: llvm.func @with_neither
; CHECK-NOT: prefix_data
; CHECK-NOT: prologue_data
; ROUNDTRIP: define i64 @with_neither() {
define i64 @with_neither() {
  ret i64 0
}
