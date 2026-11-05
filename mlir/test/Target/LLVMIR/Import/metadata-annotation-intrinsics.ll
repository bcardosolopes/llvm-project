; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s
; The metadata was declined on the way in, so the round trip is the test.
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; The annotation intrinsics take a pointer -- clang emits them from
; `__attribute__((annotate(...)))` -- so LLVM attaches the same alias metadata
; it would to any other pointer use. The ops had no slot for it, so it was
; declined: the carrier, not the property, exactly as with `llvm.fence`,
; `llvm.prefetch` and the other intrinsics handled the same way.

@.str = private constant [4 x i8] c"ann\00"
@.str.1 = private constant [4 x i8] c"f.c\00"

declare void @llvm.var.annotation.p0.p0(ptr, ptr, ptr, i32, ptr)
declare ptr @llvm.ptr.annotation.p0.p0(ptr, ptr, ptr, i32, ptr)

; CHECK-NOT: unhandled metadata
; CHECK-DAG: llvm.intr.var.annotation{{.*}}noalias_scopes = [#[[SCOPE:.+]]]
; CHECK-DAG: llvm.intr.ptr.annotation{{.*}}alias_scopes = [#[[SCOPE]]]
define void @annotated(ptr %p) {
  ; ROUNDTRIP: call void @llvm.var.annotation{{.*}}!noalias ![[SCOPES:[0-9]+]]
  call void @llvm.var.annotation.p0.p0(ptr %p, ptr @.str, ptr @.str.1, i32 7, ptr null), !noalias !0
  ; ROUNDTRIP: call ptr @llvm.ptr.annotation{{.*}}!alias.scope ![[SCOPES]]
  %q = call ptr @llvm.ptr.annotation.p0.p0(ptr %p, ptr @.str, ptr @.str.1, i32 8, ptr null), !alias.scope !0
  ret void
}

!0 = !{!1}
!1 = distinct !{!1, !2, !"s"}
!2 = distinct !{!2, !"d"}
