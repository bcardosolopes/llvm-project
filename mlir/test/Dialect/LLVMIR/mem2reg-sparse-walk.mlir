// RUN: mlir-opt %s --pass-pipeline="builtin.module(llvm.func(mem2reg{region-simplify=false}))" --split-input-file | FileCheck %s

// Mem2reg visits only the blocks that use the slot, and walks only the paths
// of the dominator tree that lead to them, to a merge point or to a merge
// point's predecessor. Blocks off those paths, and blocks on them with no use,
// still pass the reaching definition through.

// A merge point's predecessor with no use of the slot, below another block
// with no use, passes on the definition from above them; a subtree with no
// use at all is not walked.
// CHECK-LABEL: llvm.func @merge_pred_without_use
// CHECK-SAME: (%{{.*}}: i1, %{{.*}}: i1, %{{.*}}: i1)
llvm.func @merge_pred_without_use(%c1: i1, %c2: i1, %c3: i1) -> i32 {
  %one = llvm.mlir.constant(1 : i32) : i32
  // CHECK-DAG: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32)
  %zero = llvm.mlir.constant(0 : i32) : i32
  // CHECK-DAG: %[[FIVE:.*]] = llvm.mlir.constant(5 : i32)
  %five = llvm.mlir.constant(5 : i32) : i32
  // CHECK-NOT: llvm.alloca
  %slot = llvm.alloca %one x i32 : (i32) -> !llvm.ptr
  llvm.store %zero, %slot : i32, !llvm.ptr
  llvm.cond_br %c1, ^unrelated, ^split
^unrelated:
  llvm.cond_br %c2, ^u1, ^u2
^u1:
  llvm.return %one : i32
^u2:
  llvm.return %five : i32
^split:
  llvm.cond_br %c3, ^store, ^nostore
^store:
  llvm.store %five, %slot : i32, !llvm.ptr
  // CHECK: llvm.br ^[[MERGE:.*]](%[[FIVE]] : i32)
  llvm.br ^merge
^nostore:
  llvm.br ^passthru
^passthru:
  // CHECK: llvm.br ^[[MERGE]](%[[ZERO]] : i32)
  llvm.br ^merge
// CHECK: ^[[MERGE]](%[[MERGED:.*]]: i32):
^merge:
  %r = llvm.load %slot : !llvm.ptr -> i32
  // CHECK: llvm.return %[[MERGED]] : i32
  llvm.return %r : i32
}

// -----

// A use two levels below the store, under blocks with no use, reads the
// stored value.
// CHECK-LABEL: llvm.func @use_below_blocks_without_use
llvm.func @use_below_blocks_without_use(%c1: i1, %c2: i1, %x: i32) -> i32 {
  %one = llvm.mlir.constant(1 : i32) : i32
  // CHECK-NOT: llvm.alloca
  %slot = llvm.alloca %one x i32 : (i32) -> !llvm.ptr
  // CHECK: %[[X:.*]] = llvm.add
  %v = llvm.add %x, %one : i32
  llvm.store %v, %slot : i32, !llvm.ptr
  llvm.cond_br %c1, ^mid, ^other
^other:
  llvm.return %one : i32
^mid:
  llvm.cond_br %c2, ^deep, ^other2
^other2:
  llvm.return %x : i32
^deep:
  %r = llvm.load %slot : !llvm.ptr -> i32
  // CHECK: llvm.return %[[X]] : i32
  llvm.return %r : i32
}

// -----

// A loop whose back edge comes from a block with no use, nested below the
// block that stores: the header's argument takes the stored value from it.
// CHECK-LABEL: llvm.func @loop_latch_without_use
llvm.func @loop_latch_without_use(%c1: i1, %c2: i1, %n: i32) -> i32 {
  %one = llvm.mlir.constant(1 : i32) : i32
  // CHECK-DAG: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32)
  %zero = llvm.mlir.constant(0 : i32) : i32
  // CHECK-NOT: llvm.alloca
  %slot = llvm.alloca %one x i32 : (i32) -> !llvm.ptr
  llvm.store %zero, %slot : i32, !llvm.ptr
  // CHECK: llvm.br ^[[HEADER:.*]](%[[ZERO]] : i32)
  llvm.br ^header
// CHECK: ^[[HEADER]](%[[CUR:.*]]: i32):
^header:
  %cur = llvm.load %slot : !llvm.ptr -> i32
  llvm.cond_br %c1, ^body, ^exit
^body:
  // CHECK: %[[NEXT:.*]] = llvm.add %[[CUR]]
  %next = llvm.add %cur, %n : i32
  llvm.store %next, %slot : i32, !llvm.ptr
  llvm.cond_br %c2, ^latch1, ^exit
^latch1:
  llvm.br ^latch2
^latch2:
  // CHECK: llvm.br ^[[HEADER]](%[[NEXT]] : i32)
  llvm.br ^header
^exit:
  %r = llvm.load %slot : !llvm.ptr -> i32
  llvm.return %r : i32
}
