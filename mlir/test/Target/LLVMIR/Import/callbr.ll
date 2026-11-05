; RUN: mlir-translate --import-llvm %s | FileCheck %s
; RUN: mlir-translate --import-llvm %s | mlir-translate --mlir-to-llvmir \
; RUN:   | FileCheck %s --check-prefix=EXPORT

; CHECK-LABEL: llvm.func @void_asm_goto(
; CHECK-SAME: %[[ARG:.*]]: i32)
; EXPORT-LABEL: define i32 @void_asm_goto(
define i32 @void_asm_goto(i32 %x) {
entry:
  ; CHECK: llvm.callbr has_side_effects asm_dialect = att "testl $0, $0; jne ${1:l}", "r,!i,~{dirflag},~{fpsr},~{flags}"(%[[ARG]]) : (i32) -> () to ^[[FALL:.*]] [^[[ERR:.*]]] {no_unwind, srcloc = array<i64: 123>}
  ; EXPORT: callbr void asm sideeffect "testl $0, $0; jne ${1:l}", "r,!i,~{dirflag},~{fpsr},~{flags}"(i32 %{{.*}}) #[[$ATTRS:[0-9]+]]
  ; EXPORT-NEXT: to label %[[FALL:.*]] [label %[[ERR:.*]]], !srcloc ![[$SRCLOC:[0-9]+]]
  callbr void asm sideeffect "testl $0, $0; jne ${1:l}", "r,!i,~{dirflag},~{fpsr},~{flags}"(i32 %x) #0
          to label %fallthrough [label %err], !srcloc !0
fallthrough:
  ; CHECK: ^[[FALL]]:
  ret i32 0
err:
  ; CHECK: ^[[ERR]]:
  ret i32 1
}

; The result reaches every successor; a successor whose phi takes it is reached
; through a tagged forwarding block.

; CHECK-LABEL: llvm.func @output_asm_goto(
; EXPORT-LABEL: define i32 @output_asm_goto(
define i32 @output_asm_goto(i32 %x, i32 %y) {
entry:
  ; CHECK: %[[RES:.*]] = llvm.callbr asm_dialect = att "mov $1, $0; jmp ${2:l}", "=r,r,!i"(%{{.*}}) : (i32) -> i32 to ^[[FWD:.*]] [^[[DIRECT:.*]](%{{.*}} : i32)]
  ; EXPORT: %[[RES:.*]] = callbr i32 asm "mov $1, $0; jmp ${2:l}", "=r,r,!i"(i32 %{{.*}})
  ; EXPORT-NEXT: to label %[[FWD:.*]] [label %[[DIRECT:.*]]]
  %r = callbr i32 asm "mov $1, $0; jmp ${2:l}", "=r,r,!i"(i32 %x)
          to label %fall [label %direct]
  ; CHECK: ^[[FWD]]:
  ; CHECK: llvm.br ^[[FALL:.*]](%[[RES]] : i32) {llvm_forwarding_branch}
  ; EXPORT: [[FWD]]:
  ; EXPORT-NEXT: br label %{{.*}}, !llvm_forwarding_branch
fall:
  ; CHECK: ^[[FALL]](%[[P:.*]]: i32):
  %p = phi i32 [ %r, %entry ]
  ret i32 %p
direct:
  ; CHECK: ^[[DIRECT]](%[[Q:.*]]: i32):
  ; CHECK: llvm.add %[[Q]], %[[RES]]
  ; EXPORT: [[DIRECT]]:
  ; EXPORT-NEXT: phi i32 [ %{{.*}}, %{{.*}} ]
  ; EXPORT-NEXT: add i32 %{{.*}}, %[[RES]]
  %q = phi i32 [ %y, %entry ]
  %s = add i32 %q, %r
  ret i32 %s
}

; One block as both the default and an indirect destination, with a phi.

; CHECK-LABEL: llvm.func @repeated_destination(
; EXPORT-LABEL: define i32 @repeated_destination(
define i32 @repeated_destination(i32 %x) {
entry:
  ; CHECK: llvm.callbr asm_dialect = att "", "r,!i"(%{{.*}}) : (i32) -> () to ^[[JOIN:.*]](%{{.*}} : i32) [^[[JOIN]](%{{.*}} : i32)]
  ; EXPORT: callbr void asm "", "r,!i"(i32 %{{.*}})
  ; EXPORT-NEXT: to label %[[JOIN:.*]] [label %[[REPEAT:.*]]]
  callbr void asm "", "r,!i"(i32 %x) to label %join [label %join]
join:
  %p = phi i32 [ %x, %entry ], [ %x, %entry ]
  ret i32 %p
}

; CHECK-LABEL: llvm.func @memory_operand(
; EXPORT-LABEL: define void @memory_operand(
define void @memory_operand(ptr %p) {
entry:
  ; CHECK: llvm.callbr has_side_effects asm_dialect = att operand_attrs = [{elementtype = i32}] "incl $0; jo ${1:l}", "=*m,!i"(%{{.*}}) : (!llvm.ptr) -> () to ^{{.*}} [^{{.*}}]
  ; EXPORT: callbr void asm sideeffect "incl $0; jo ${1:l}", "=*m,!i"(ptr elementtype(i32) %{{.*}})
  callbr void asm sideeffect "incl $0; jo ${1:l}", "=*m,!i"(ptr elementtype(i32) %p)
          to label %done [label %overflow]
done:
  ret void
overflow:
  ret void
}

; EXPORT: attributes #[[$ATTRS]] = { nounwind }
; EXPORT: ![[$SRCLOC]] = !{i64 123}

attributes #0 = { nounwind }
!0 = !{i64 123}
