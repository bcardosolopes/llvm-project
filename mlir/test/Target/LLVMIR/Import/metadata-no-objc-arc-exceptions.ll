; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s

; !clang.arc.no_objc_arc_exceptions tells the ObjC ARC optimizer it may ignore
; the unwind edge of a call. Clang attaches it only under ObjC ARC, and always
; to a CallBase, so llvm.call and llvm.invoke are the two carriers. The node is
; valueless -- its presence is the whole content -- and the kind is registered
; by name rather than being a fixed one.
;
; A census of real IR found 71 sites of it in a single library.

; CHECK-NOT: dropped metadata
; CHECK-NOT: unhandled metadata

declare void @a()
declare i32 @__gxx_personality_v0(...)

; CHECK-LABEL: llvm.func @calls
define void @calls() personality ptr @__gxx_personality_v0 {
  ; CHECK: llvm.call @a() {no_objc_arc_exceptions}
  call void @a(), !clang.arc.no_objc_arc_exceptions !0
  ; CHECK: llvm.invoke @a() to ^bb1 unwind ^bb2 {no_objc_arc_exceptions}
  invoke void @a() to label %done unwind label %lpad, !clang.arc.no_objc_arc_exceptions !0
done:
  ret void
lpad:
  %ex = landingpad { ptr, i32 } cleanup
  ret void
}

; An unmarked call must stay unmarked.
; CHECK-LABEL: llvm.func @plain
; CHECK: llvm.call @a()
; CHECK-NOT: no_objc_arc_exceptions
define void @plain() {
  call void @a()
  ret void
}

!0 = !{}
