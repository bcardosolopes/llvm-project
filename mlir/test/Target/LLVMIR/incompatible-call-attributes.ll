; A call or invoke whose type differs from its callee's is imported as an
; indirect call through the callee's address, and keeps its parameter and
; result attributes: they index the call's own arguments. The IR linker makes
; such a call whenever two modules disagree on a function's type (a template
; instantiated with different types, a C function declared without a
; prototype), and the attributes it drops let LLVM assume less about the
; arguments (nonnull, dereferenceable, noundef).

; RUN: mlir-translate -import-llvm %s | FileCheck --check-prefix=IMPORT %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s

; IMPORT-LABEL: llvm.func @mismatched_call(
; IMPORT: %[[TARGET:.*]] = llvm.mlir.addressof @callee : !llvm.ptr
; IMPORT: llvm.call %[[TARGET]](%{{.*}}) : !llvm.ptr, (!llvm.ptr {llvm.align = 8 : i64, llvm.dereferenceable = 8 : i64, llvm.nonnull, llvm.noundef}) -> (i64 {llvm.noundef})
; CHECK-LABEL: define i64 @mismatched_call(
; CHECK: call noundef i64 @callee(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}})
define i64 @mismatched_call(ptr %p) {
  %r = call noundef i64 @callee(ptr noundef nonnull align 8 dereferenceable(8) %p)
  ret i64 %r
}

; IMPORT-LABEL: llvm.func @mismatched_invoke(
; IMPORT: llvm.invoke %{{.*}}(%{{.*}}) to ^{{.*}} unwind ^{{.*}} : !llvm.ptr, (!llvm.ptr {llvm.nonnull, llvm.noundef}) -> (i64 {llvm.noundef})
; CHECK-LABEL: define i64 @mismatched_invoke(
; CHECK: invoke noundef i64 @callee(ptr noundef nonnull %{{.*}})
define i64 @mismatched_invoke(ptr %p) personality ptr @__gxx_personality_v0 {
entry:
  %r = invoke noundef i64 @callee(ptr noundef nonnull %p)
          to label %ok unwind label %lpad
ok:
  ret i64 %r
lpad:
  %lp = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %lp
}

define ptr @callee(ptr %p) {
  ret ptr %p
}

declare i32 @__gxx_personality_v0(...)
