; RUN: mlir-translate -import-llvm %s | FileCheck %s
; Both were dropped in each direction, so the round trip is the test.
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

; `!callback` is function-only. Each entry is the callee argument index, the
; payload argument indices, then whether the varargs are forwarded -- that last
; operand is an i1, not another index, so it is kept apart from them.
; CHECK: llvm.func @broker
; CHECK-SAME: llvm.callback = {{\[}}[array<i64: 0, 1>, false]]
; ROUNDTRIP: declare !callback ![[CB:[0-9]+]] void @broker
declare !callback !4 void @broker(ptr, ptr)

; `!pcsections` is legal on a function *and* on an instruction, so it rides as
; one attribute written back from both export paths.
; CHECK: llvm.func @f
; CHECK-SAME: llvm.pcsections = {{\[}}["section_aux_42", array<i32: 42>], ["section_aux_21264", array<i32: 21264>]]
; ROUNDTRIP: define i64 @f(ptr %{{.*}}) !pcsections ![[FSEC:[0-9]+]]
define i64 @f(ptr %p) !pcsections !1 {
  ; A section with no auxiliary data is written as the name alone -- no empty
  ; node -- which is the shape it came in as.
  ; CHECK: llvm.load {{.*}}llvm.pcsections = {{\[}}["section_no_aux", array<i32>]]
  ; ROUNDTRIP: load i64, ptr %{{.*}}, !pcsections ![[ISEC:[0-9]+]]
  %v = load i64, ptr %p, !pcsections !0
  ret i64 %v
}

!0 = !{!"section_no_aux"}
!1 = !{!"section_aux_42", !2, !"section_aux_21264", !3}
!2 = !{i32 42}
!3 = !{i32 21264}
!4 = !{!5}
!5 = !{i64 0, i64 1, i1 false}

; ROUNDTRIP-DAG: ![[CB]] = !{![[ENC:[0-9]+]]}
; ROUNDTRIP-DAG: ![[ENC]] = !{i64 0, i64 1, i1 false}
; ROUNDTRIP-DAG: ![[FSEC]] = !{!"section_aux_42", ![[A1:[0-9]+]], !"section_aux_21264", ![[A2:[0-9]+]]}
; ROUNDTRIP-DAG: ![[A1]] = !{i32 42}
; ROUNDTRIP-DAG: ![[A2]] = !{i32 21264}
; ROUNDTRIP-DAG: ![[ISEC]] = !{!"section_no_aux"}
