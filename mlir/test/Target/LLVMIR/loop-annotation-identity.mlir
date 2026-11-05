// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s

// The identity on `loop_annotation` exists for the export, so the export is
// what has to be tested from MLIR. Coming in through `-import-llvm` cannot
// show it: the importer mints an id per `!llvm.loop` node, so the annotations
// are already distinct before the export runs and the collapse never happens.

// Two loops whose options are identical and whose ids are not. A `!llvm.loop`
// node is a loop's identity -- LLVM builds it `distinct` and passes use it to
// record that a transformation has run -- so these must not share one.
// CHECK: define void @two_loops_distinct_ids
llvm.func @two_loops_distinct_ids(%c: i1) {
  llvm.br ^bb1
^bb1:
  // CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !llvm.loop ![[L1:[0-9]+]]
  llvm.cond_br %c, ^bb1, ^bb2 {loop_annotation = #llvm.loop_annotation<unroll = <disable = true>, id = distinct[0]<>>}
^bb2:
  // CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !llvm.loop ![[L2:[0-9]+]]
  llvm.cond_br %c, ^bb2, ^bb3 {loop_annotation = #llvm.loop_annotation<unroll = <disable = true>, id = distinct[1]<>>}
^bb3:
  llvm.return
}

// One loop with two latches. Both carry the same annotation, so both must name
// the same node -- that is what makes them one loop rather than two.
// CHECK: define void @one_loop_two_latches
llvm.func @one_loop_two_latches(%c: i1) {
  llvm.br ^head
^head:
  llvm.cond_br %c, ^latchA, ^latchB
^latchA:
  // CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !llvm.loop ![[SHARED:[0-9]+]]
  llvm.cond_br %c, ^head, ^done {loop_annotation = #llvm.loop_annotation<unroll = <disable = true>, id = distinct[2]<>>}
^latchB:
  // CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !llvm.loop ![[SHARED]]
  llvm.cond_br %c, ^head, ^done {loop_annotation = #llvm.loop_annotation<unroll = <disable = true>, id = distinct[2]<>>}
^done:
  llvm.return
}

// The id is optional, and without one the attribute is purely structural: two
// identical annotations are one attribute and so export as one node. That is
// the documented fallback, not an oversight -- an annotation built by a
// conversion from a higher dialect has no basis for inventing an identity.
// CHECK: define void @two_loops_no_ids
llvm.func @two_loops_no_ids(%c: i1) {
  llvm.br ^bb1
^bb1:
  // CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !llvm.loop ![[NOID:[0-9]+]]
  llvm.cond_br %c, ^bb1, ^bb2 {loop_annotation = #llvm.loop_annotation<unroll = <disable = true>>}
^bb2:
  // CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !llvm.loop ![[NOID]]
  llvm.cond_br %c, ^bb2, ^bb3 {loop_annotation = #llvm.loop_annotation<unroll = <disable = true>>}
^bb3:
  llvm.return
}

// Plain `CHECK` above, not `CHECK-LABEL`: a label starts a new block and the
// captures would not be in scope for these definitions.
//
// Each is self-referential and distinct. Two `-DAG` lines with the same
// pattern must match two different lines, which is what pins L1 and L2 apart.
// CHECK-DAG: ![[L1]] = distinct !{![[L1]], !{{[0-9]+}}}
// CHECK-DAG: ![[L2]] = distinct !{![[L2]], !{{[0-9]+}}}
// CHECK-DAG: ![[SHARED]] = distinct !{![[SHARED]], !{{[0-9]+}}}
// CHECK-DAG: ![[NOID]] = distinct !{![[NOID]], !{{[0-9]+}}}
