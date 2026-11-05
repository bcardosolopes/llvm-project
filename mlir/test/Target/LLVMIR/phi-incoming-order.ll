; A phi keeps the order of its incoming values through the round trip. A
; terminator records, for each successor, the position of its edge among the
; successor's phi incoming values (`llvm.phi_positions`) where that order is
; not the one the export uses by default: the predecessors in reverse block
; order, each terminator's successors last to first.

; RUN: mlir-translate -import-llvm %s | FileCheck --check-prefix=IMPORT %s
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir \
; RUN:   | FileCheck %s

; Fixed point: pass 1 and pass 2 must be byte-identical.
; RUN: mlir-translate -import-llvm %s | mlir-translate -mlir-to-llvmir > %t.pass1.ll
; RUN: mlir-translate -import-llvm %t.pass1.ll | mlir-translate -mlir-to-llvmir > %t.pass2.ll
; RUN: diff %t.pass1.ll %t.pass2.ll

; IMPORT-LABEL: llvm.func @layout_order(
; IMPORT: llvm.br ^bb3({{.*}}) {llvm.phi_positions = array<i32: 0>}
; IMPORT: llvm.br ^bb3({{.*}}) {llvm.phi_positions = array<i32: 1>}
; CHECK-LABEL: define i32 @layout_order(
; CHECK: phi i32 [ 1, %{{[0-9]+}} ], [ 2, %{{[0-9]+}} ]
; CHECK: phi i32 [ 3, %{{[0-9]+}} ], [ 4, %{{[0-9]+}} ]
define i32 @layout_order(i1 %c) {
entry:
  br i1 %c, label %a, label %b
a:
  br label %join
b:
  br label %join
join:
  %p = phi i32 [ 1, %a ], [ 2, %b ]
  %q = phi i32 [ 3, %a ], [ 4, %b ]
  %r = add i32 %p, %q
  ret i32 %r
}

; The default order needs no attribute.
; IMPORT-LABEL: llvm.func @default_order(
; IMPORT-NOT: llvm.phi_positions
; IMPORT: llvm.return
; CHECK-LABEL: define i32 @default_order(
; CHECK: phi i32 [ 2, %{{[0-9]+}} ], [ 1, %{{[0-9]+}} ]
define i32 @default_order(i1 %c) {
entry:
  br i1 %c, label %a, label %b
a:
  br label %join
b:
  br label %join
join:
  %p = phi i32 [ 2, %b ], [ 1, %a ]
  ret i32 %p
}

; A switch edge and branch edges, interleaved.
; IMPORT-LABEL: llvm.func @switch_order(
; IMPORT: llvm.switch
; IMPORT: ] {llvm.phi_positions = array<i32: -1, -1, -1, 1>}
; CHECK-LABEL: define i32 @switch_order(
; CHECK: phi i32 [ 7, %{{[0-9]+}} ], [ 5, %{{[0-9]+}} ], [ 9, %{{[0-9]+}} ], [ 6, %{{[0-9]+}} ]
define i32 @switch_order(i32 %v) {
entry:
  switch i32 %v, label %d [ i32 0, label %x
                            i32 1, label %y
                            i32 2, label %join ]
x:
  br label %join
y:
  br label %join
d:
  br label %join
join:
  %p = phi i32 [ 7, %y ], [ 5, %entry ], [ 9, %d ], [ 6, %x ]
  ret i32 %p
}

; A loop header whose back edge comes first.
; CHECK-LABEL: define i32 @loop_order(
; CHECK: phi i32 [ %{{[0-9]+}}, %{{[0-9]+}} ], [ 0, %{{[0-9]+}} ]
define i32 @loop_order(i32 %n) {
entry:
  br label %h
h:
  %i = phi i32 [ %i1, %h ], [ 0, %entry ]
  %i1 = add i32 %i, 1
  %c = icmp slt i32 %i1, %n
  br i1 %c, label %h, label %e
e:
  ret i32 %i
}
