// RUN: mlir-translate -mlir-to-llvmir -split-input-file %s | FileCheck %s
// RUN: mlir-opt %s -split-input-file --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-callees.ll.

llvm.func @a()
llvm.func @b()

// CHECK-LABEL: @indirect_call
llvm.func @indirect_call(%fp: !llvm.ptr) {
  // CHECK: call void %{{.*}}(), !callees ![[C:[0-9]+]]
  llvm.call %fp() {callees = [@a, @b]} : !llvm.ptr, () -> ()
  llvm.return
}

// CHECK: ![[C]] = !{ptr @a, ptr @b}

// -----

llvm.func @a()
llvm.func @__gxx_personality_v0(...) -> i32

// CHECK-LABEL: @indirect_invoke
llvm.func @indirect_invoke(%fp: !llvm.ptr)
    attributes {personality = @__gxx_personality_v0} {
  // CHECK: invoke void %{{.*}}()
  // CHECK: !callees ![[C:[0-9]+]]
  llvm.invoke %fp() to ^bb1 unwind ^bb2 {callees = [@a]} : !llvm.ptr, () -> ()
^bb1:
  llvm.return
^bb2:
  %0 = llvm.landingpad cleanup : !llvm.struct<(ptr, i32)>
  llvm.return
}

// CHECK: ![[C]] = !{ptr @a}

// -----

// A symbol that no longer resolves is dropped from the list rather than
// failing the export: the node is a hint, and a stale entry is worse than a
// shorter one.

llvm.func @a()

// CHECK-LABEL: @partly_stale
llvm.func @partly_stale(%fp: !llvm.ptr) {
  // CHECK: call void %{{.*}}(), !callees ![[C:[0-9]+]]
  llvm.call %fp() {callees = [@a, @gone]} : !llvm.ptr, () -> ()
  llvm.return
}

// CHECK: ![[C]] = !{ptr @a}
