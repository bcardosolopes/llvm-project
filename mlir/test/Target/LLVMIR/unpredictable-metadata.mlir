// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s
// RUN: mlir-opt %s --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-unpredictable.ll. The node is valueless,
// so the presence of the attribute is the whole content.

// CHECK: define i32 @carriers
llvm.func @carriers(%c: i1, %x: i32, %y: i32) -> i32 {
  // CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !unpredictable ![[EMPTY:[0-9]+]]
  llvm.cond_br %c, ^bb1, ^bb2 {unpredictable}
^bb1:
  // CHECK: select i1 %{{.*}}, i32 %{{.*}}, i32 %{{.*}}, !unpredictable ![[EMPTY]]
  %0 = llvm.select %c, %x, %y {unpredictable} : i1, i32
  llvm.return %0 : i32
^bb2:
  %1 = llvm.mlir.constant(0 : i32) : i32
  llvm.return %1 : i32
}

// CHECK: define void @switch_carrier
llvm.func @switch_carrier(%v: i32) {
  // CHECK: ], !unpredictable ![[EMPTY]]
  llvm.switch %v : i32, ^bb1 [
    0: ^bb1
  ] {unpredictable}
^bb1:
  llvm.return
}

// An unmarked branch must stay unmarked.
// CHECK: define void @not_marked
llvm.func @not_marked(%c: i1) {
  // CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}
  // CHECK-NOT: !unpredictable
  llvm.cond_br %c, ^bb1, ^bb2
^bb1:
  llvm.return
^bb2:
  llvm.return
}

// CHECK: ![[EMPTY]] = !{}
