; RUN: mlir-translate -import-llvm %s | FileCheck %s
; The flag is dropped on the way out if it is not modelled, so the round trip
; is the real test.
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; CHECK-LABEL: @icmp_samesign
define i1 @icmp_samesign(i32 %a, i32 %b) {
  ; CHECK: llvm.icmp "ult" %{{.*}}, %{{.*}} {isSameSign} : i32
  ; ROUNDTRIP: icmp samesign ult i32
  %c = icmp samesign ult i32 %a, %b
  ret i1 %c
}

; CHECK-LABEL: @icmp_plain
define i1 @icmp_plain(i32 %a, i32 %b) {
  ; CHECK: llvm.icmp "ult" %{{.*}}, %{{.*}} : i32
  ; CHECK-NOT: isSameSign
  ; ROUNDTRIP: icmp ult i32
  %c = icmp ult i32 %a, %b
  ret i1 %c
}
