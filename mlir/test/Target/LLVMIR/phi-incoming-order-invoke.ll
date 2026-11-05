; A phi takes its incoming order through the forwarding block the import
; adds between an invoke and a normal destination that takes the invoke's
; result: that block's branch records the edge (`llvm.phi_positions`). Not a
; fixed point like phi-incoming-order.ll: the plain export keeps the
; forwarding block, which the whole-app step folds back.

; RUN: mlir-translate -import-llvm %s | FileCheck --check-prefix=IMPORT %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s

; IMPORT-LABEL: llvm.func @invoke_order(
; IMPORT: llvm.br ^bb{{[0-9]+}}({{.*}}) {llvm.phi_positions = array<i32: 0>}
; IMPORT: llvm.br ^bb{{[0-9]+}}({{.*}}) {llvm.phi_positions = array<i32: 1>, llvm_forwarding_branch}
; CHECK-LABEL: define i8 @invoke_order(
; CHECK: phi i8 [ 0, %{{[0-9]+}} ], [ %{{[0-9]+}}, %{{[0-9]+}} ]
declare i8 @produce()
declare i32 @__gxx_personality_v0(...)
define i8 @invoke_order(i1 %c) personality ptr @__gxx_personality_v0 {
entry:
  br i1 %c, label %inv, label %other
inv:
  %call = invoke i8 @produce() to label %join unwind label %lpad
other:
  br label %join
join:
  %p = phi i8 [ 0, %other ], [ %call, %inv ]
  ret i8 %p
lpad:
  %lp = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %lp
}
