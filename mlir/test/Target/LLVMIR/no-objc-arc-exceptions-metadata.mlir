// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s
// RUN: mlir-opt %s --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-no-objc-arc-exceptions.ll. The body is
// exactly what the importer produces, so the two pin the whole trip.

// CHECK: call void @a(), !clang.arc.no_objc_arc_exceptions ![[E:[0-9]+]]
// CHECK: unwind label %{{.*}}, !clang.arc.no_objc_arc_exceptions ![[E]]
// CHECK: ![[E]] = !{}

module {
  llvm.func @a()
  llvm.func @__gxx_personality_v0(...) -> i32
  llvm.func @calls() attributes {personality = @__gxx_personality_v0} {
    llvm.call @a() {no_objc_arc_exceptions} : () -> ()
    llvm.invoke @a() to ^bb1 unwind ^bb2 {no_objc_arc_exceptions} : () -> ()
  ^bb1:  // pred: ^bb0
    llvm.return
  ^bb2:  // pred: ^bb0
    %0 = llvm.landingpad cleanup : !llvm.struct<(ptr, i32)>
    llvm.return
  }
  llvm.func @plain() {
    llvm.call @a() : () -> ()
    llvm.return
  }
}
