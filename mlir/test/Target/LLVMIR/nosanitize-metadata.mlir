// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s
// RUN: mlir-opt %s --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-nosanitize.ll. The attribute goes on
// every instruction the op produced, because an op can expand into several
// accesses that must each stay uninstrumented.

llvm.func @f() -> !llvm.ptr

// CHECK: define void @nosan
llvm.func @nosan(%p: !llvm.ptr) {
  // CHECK: load i32, ptr %{{.*}}, align 4, !nosanitize ![[EMPTY:[0-9]+]]
  %0 = llvm.load %p {alignment = 4 : i64, llvm.nosanitize} : !llvm.ptr -> i32
  // CHECK: store i32 %{{.*}}, ptr %{{.*}}, align 4, !nosanitize ![[EMPTY]]
  llvm.store %0, %p {alignment = 4 : i64, llvm.nosanitize} : i32, !llvm.ptr
  // CHECK: call ptr @f(), !nosanitize ![[EMPTY]]
  %1 = llvm.call @f() {llvm.nosanitize} : () -> !llvm.ptr
  // CHECK: add i32 %{{.*}}, 1, !nosanitize ![[EMPTY]]
  %2 = llvm.mlir.constant(1 : i32) : i32
  %3 = llvm.add %0, %2 {llvm.nosanitize} : i32
  llvm.return
}

// An unmarked access must stay unmarked.
// CHECK: define i32 @plain
llvm.func @plain(%p: !llvm.ptr) -> i32 {
  // CHECK: load i32, ptr %{{.*}}, align 4
  // CHECK-NOT: !nosanitize
  %0 = llvm.load %p {alignment = 4 : i64} : !llvm.ptr -> i32
  llvm.return %0 : i32
}

// CHECK: ![[EMPTY]] = !{}
