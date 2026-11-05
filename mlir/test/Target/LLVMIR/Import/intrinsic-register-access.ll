; RUN: mlir-translate -import-llvm -emit-expensive-warnings %s 2>&1 | FileCheck %s \
; RUN:   --implicit-check-not="dropped instruction"
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; `llvm.read_register` and `llvm.write_register` name a machine register with
; an `MDString` operand rather than a value, so the importer had no path for
; them and declined the call. Unlike `llvm.allow.runtime.check`, the string is
; wrapped in a node -- `metadata !{!"sp"}` -- so the wrapper is unpacked on the
; way in and rebuilt on the way out.

declare i64 @llvm.read_register.i64(metadata)
declare void @llvm.write_register.i64(metadata, i64)
declare i64 @llvm.read_volatile_register.i64(metadata)

; CHECK-LABEL: @registers
; CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0 : i64) : i64
define i64 @registers(i64 %v) {
  ; CHECK: llvm.intr.read_register "sp" : i64
  %sp = call i64 @llvm.read_register.i64(metadata !0)
  ; Different register, so it must not collapse onto the one above.
  ; CHECK: llvm.intr.write_register "x28", %{{.*}} : i64
  call void @llvm.write_register.i64(metadata !1, i64 %v)
  ; CHECK: llvm.intr.write_register "x28", %[[ZERO]] : i64
  call void @llvm.write_register.i64(metadata !1, i64 0)
  ; The volatile read is its own op, not a flag: the difference between the two
  ; is `IntrHasSideEffects` against `IntrReadMem`, which an op states in its
  ; traits and so cannot vary per instance.
  ; CHECK: llvm.intr.read_volatile_register "sp" : i64
  %vsp = call i64 @llvm.read_volatile_register.i64(metadata !0)
  ret i64 %sp
}

; The node is rebuilt around the string, not written as a bare one.
; ROUNDTRIP: call i64 @llvm.read_register.i64(metadata ![[SP:[0-9]+]])
; ROUNDTRIP: call void @llvm.write_register.i64(metadata ![[X28:[0-9]+]], i64 %{{.*}})
; ROUNDTRIP: call void @llvm.write_register.i64(metadata ![[X28]], i64 0)
; ROUNDTRIP: call i64 @llvm.read_volatile_register.i64(metadata ![[SP]])
; ROUNDTRIP-DAG: ![[SP]] = !{!"sp"}
; ROUNDTRIP-DAG: ![[X28]] = !{!"x28"}

!0 = !{!"sp"}
!1 = !{!"x28"}
