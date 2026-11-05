// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s
// RUN: mlir-opt %s --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-nonnull.ll. The node is valueless, so
// the presence of the attribute is the whole content.

// CHECK: define ptr @nonnull_load
llvm.func @nonnull_load(%p: !llvm.ptr) -> !llvm.ptr {
  // CHECK: load ptr, ptr %{{.*}}, align 8, !nonnull ![[EMPTY:[0-9]+]]
  %0 = llvm.load %p {alignment = 8 : i64, nonnull} : !llvm.ptr -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// CHECK: define ptr @plain_load
llvm.func @plain_load(%p: !llvm.ptr) -> !llvm.ptr {
  // CHECK: load ptr, ptr %{{.*}}, align 8
  // CHECK-NOT: !nonnull
  %0 = llvm.load %p {alignment = 8 : i64} : !llvm.ptr -> !llvm.ptr
  llvm.return %0 : !llvm.ptr
}

// CHECK: ![[EMPTY]] = !{}
