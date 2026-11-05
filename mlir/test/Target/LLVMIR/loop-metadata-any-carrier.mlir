// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s
// RUN: mlir-opt %s --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-loop-any-carrier.ll. The body is exactly
// what the importer produces, so the two pin the whole trip. All five
// attachments must come back, and all must point at the same loop node.

// CHECK: load i32, ptr %{{.*}}, align 4, !llvm.loop ![[L:[0-9]+]]
// CHECK: store i32 %{{.*}}, ptr %{{.*}}, align 4, !llvm.loop ![[L]]
// CHECK: add i32 %{{.*}}, 1, !llvm.loop ![[L]]
// CHECK: call void @body(), !llvm.loop ![[L]]
// CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !llvm.loop ![[L]]
// CHECK: ![[L]] = distinct !{![[L]], ![[MP:[0-9]+]]}
// CHECK: ![[MP]] = !{!"llvm.loop.mustprogress"}

#loop_annotation = #llvm.loop_annotation<mustProgress = true>
module {
  llvm.func @body()
  llvm.func @many_carriers(%arg0: !llvm.ptr, %arg1: i1) {
    %0 = llvm.mlir.constant(1 : i32) : i32
    llvm.br ^bb1
  ^bb1:  // 2 preds: ^bb0, ^bb1
    %1 = llvm.load %arg0 {alignment = 4 : i64, llvm.loop_annotation = #loop_annotation} : !llvm.ptr -> i32
    llvm.store %1, %arg0 {alignment = 4 : i64, llvm.loop_annotation = #loop_annotation} : i32, !llvm.ptr
    %2 = llvm.add %1, %0 {llvm.loop_annotation = #loop_annotation} : i32
    llvm.call @body() {llvm.loop_annotation = #loop_annotation} : () -> ()
    llvm.cond_br %arg1, ^bb1, ^bb2 {loop_annotation = #loop_annotation}
  ^bb2:  // pred: ^bb1
    llvm.return
  }
}
