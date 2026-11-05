// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s
// RUN: mlir-opt %s --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-loop-invoke.ll.

#loopAnnotation = #llvm.loop_annotation<mustProgress = true>

llvm.func @body()
llvm.func @__gxx_personality_v0(...) -> i32

// CHECK: define void @loop_with_invoke_latch
llvm.func @loop_with_invoke_latch()
    attributes {personality = @__gxx_personality_v0} {
  llvm.br ^latch
^latch:
  // CHECK: invoke void @body()
  // CHECK: unwind label %{{.*}}, !llvm.loop ![[LOOP:[0-9]+]]
  llvm.invoke @body() to ^latch unwind ^lpad
      {loop_annotation = #loopAnnotation} : () -> ()
^lpad:
  %0 = llvm.landingpad cleanup : !llvm.struct<(ptr, i32)>
  llvm.return
}

// CHECK: ![[LOOP]] = distinct !{![[LOOP]], ![[MP:[0-9]+]]}
// CHECK: ![[MP]] = !{!"llvm.loop.mustprogress"}
