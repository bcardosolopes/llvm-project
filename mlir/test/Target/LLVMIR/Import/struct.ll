; RUN: mlir-translate -import-llvm -split-input-file %s | FileCheck %s

%"bucket<string, double, '\\b'>::Iterator" = type { ptr, i64, i64 }

; CHECK-LABEL: llvm.func @g
define void @g() {
  %item.i = alloca %"bucket<string, double, '\\b'>::Iterator", align 8
  ; CHECK: llvm.alloca %0 x !llvm.struct<"bucket<string, double, '\\b'>::Iterator", (ptr, i64, i64)> {alignment = 8 : i64} : (i32) -> !llvm.ptr
  ret void
}

; // -----

; LLVM keeps two types of one name apart with a numeric suffix. A suffixed type
; of the same body is imported as the type without the suffix, whichever comes
; first; one of another body keeps its LLVM name.
%struct.same = type { ptr, i32 }
%struct.same.0 = type { ptr, i32 }
%struct.other = type { i32 }
%struct.other.0 = type { i64 }

; CHECK-LABEL: llvm.func @suffixed
define void @suffixed() {
  ; CHECK: llvm.alloca %{{.*}} x !llvm.struct<"struct.same", (ptr, i32)>
  %1 = alloca %struct.same.0
  ; CHECK: llvm.alloca %{{.*}} x !llvm.struct<"struct.same", (ptr, i32)>
  %2 = alloca %struct.same
  ; CHECK: llvm.alloca %{{.*}} x !llvm.struct<"struct.other", (i32)>
  %3 = alloca %struct.other
  ; CHECK: llvm.alloca %{{.*}} x !llvm.struct<"struct.other.0", (i64)>
  %4 = alloca %struct.other.0
  ret void
}

; // -----

; A type without a suffix whose name an earlier suffixed type of another body
; took gets a new numeric suffix.
%struct.taken.0 = type { i32 }
%struct.taken = type { i64 }

; CHECK-LABEL: llvm.func @taken
define void @taken() {
  ; CHECK: llvm.alloca %{{.*}} x !llvm.struct<"struct.taken", (i32)>
  %1 = alloca %struct.taken.0
  ; CHECK: llvm.alloca %{{.*}} x !llvm.struct<"struct.taken.1", (i64)>
  %2 = alloca %struct.taken
  ret void
}

; // -----

; A body an earlier suffixed type put under an opaque type's name belongs to
; another LLVM type, so the opaque type stays opaque, under a new name: given
; the body, the external global would become dereferenceable.
%struct.opaque.0 = type { i32 }
%struct.opaque = type opaque

; CHECK: llvm.mlir.global external @bodied()
; CHECK-SAME: !llvm.struct<"struct.opaque", (i32)>
@bodied = external global %struct.opaque.0
; CHECK: llvm.mlir.global external @opaque()
; CHECK-SAME: !llvm.struct<"struct.opaque.0", opaque>
@opaque = external global %struct.opaque
