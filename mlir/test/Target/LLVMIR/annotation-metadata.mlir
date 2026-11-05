// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s
// RUN: mlir-opt %s -split-input-file --verify-roundtrip -o /dev/null

// The `annotations` attribute is exported back to !annotation on every
// instruction the op produced.

llvm.func @sink(!llvm.ptr)

// CHECK-LABEL: define void @annotation_flat
llvm.func @annotation_flat(%arg0: !llvm.ptr) {
  %c = llvm.mlir.constant(7 : i32) : i32
  // CHECK: store i32 7, {{.*}} !annotation ![[FLAT:[0-9]+]]
  llvm.store %c, %arg0 {annotations = ["auto-init"]} : i32, !llvm.ptr
  // CHECK: call void @sink({{.*}} !annotation ![[FLAT]]
  llvm.call @sink(%arg0) {annotations = ["auto-init"]} : (!llvm.ptr) -> ()
  llvm.return
}

// CHECK-DAG: ![[FLAT]] = !{!"auto-init"}

// -----

llvm.func @sink(!llvm.ptr)

// CHECK-LABEL: define void @annotation_nested
llvm.func @annotation_nested(%arg0: !llvm.ptr) {
  // CHECK: call void @sink({{.*}} !annotation ![[N:[0-9]+]]
  llvm.call @sink(%arg0) {annotations = [["kind", "detail"], "plain"]} : (!llvm.ptr) -> ()
  llvm.return
}

// CHECK-DAG: ![[N]] = !{![[INNER:[0-9]+]], !"plain"}
// CHECK-DAG: ![[INNER]] = !{!"kind", !"detail"}
