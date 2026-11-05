; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s

; Every id here is a hash of a source location, so the whole 64-bit pattern is
; meaningful and roughly half of the real ones have bit 63 set. The import
; reads them with `getZExtValue` and keeps them in a `DenseI64ArrayAttr`, where
; they print signed; the export has to put the same bits back. A range check on
; either side, or a signed build of the constant, would corrupt half a real
; profile while leaving the small-id tests passing, so chain the two directions
; and pin the bits.

; CHECK-LABEL: @hash_ids
define ptr @hash_ids() {
  ; CHECK: call ptr @alloc({{.*}}), !memprof ![[MEMPROF:[0-9]+]], !callsite ![[STACK:[0-9]+]]
  %p = call ptr @alloc(i64 16), !memprof !0, !callsite !2
  ret ptr %p
}

declare ptr @alloc(i64)

!0 = !{!1}
!1 = !{!2, !"cold", !3}
!2 = !{i64 -1, i64 -9223372036854775808}
!3 = !{i64 -9223372036854775808, i64 4096}

; CHECK-DAG: ![[MEMPROF]] = !{![[MIB:[0-9]+]]}
; CHECK-DAG: ![[MIB]] = !{![[STACK]], !"cold", ![[SIZES:[0-9]+]]}
; CHECK-DAG: ![[STACK]] = !{i64 -1, i64 -9223372036854775808}
; CHECK-DAG: ![[SIZES]] = !{i64 -9223372036854775808, i64 4096}
