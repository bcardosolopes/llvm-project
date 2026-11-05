// RUN: mlir-translate -mlir-to-llvmir %s | FileCheck %s
// RUN: mlir-opt %s --verify-roundtrip -o /dev/null

// The export mirror of Import/metadata-loop-inlined.ll. A loop location that
// came from an inlined body is a CallSiteLoc, and the DILocalScope the node
// needs lives on its callee side rather than directly on the location.
//
// The body below is exactly what the importer produces for that input, so the
// two tests together pin the whole round trip.

// CHECK-LABEL: @inlined_loop
// CHECK: br label %{{.*}}, !llvm.loop ![[LOOP:[0-9]+]]

// The node keeps both DILocations, and each keeps its inlinedAt.
// CHECK-DAG: ![[LOOP]] = distinct !{![[LOOP]], ![[START:[0-9]+]], ![[END:[0-9]+]], ![[NONFORCED:[0-9]+]]}
// CHECK-DAG: ![[START]] = !DILocation(line: 3, column: 3, scope: ![[INLINEE:[0-9]+]], inlinedAt: ![[AT:[0-9]+]])
// CHECK-DAG: ![[INLINEE]] = distinct !DISubprogram(name: "inlinee"
// CHECK-DAG: ![[AT]] = !DILocation(line: 9, column: 1, scope: !{{[0-9]+}})
// CHECK-DAG: ![[END]] = !DILocation(line: 5, column: 3, scope: ![[INLINEE]], inlinedAt: ![[AT]])
// CHECK-DAG: ![[NONFORCED]] = !{!"llvm.loop.disable_nonforced"}

#di_file = #llvm.di_file<"l.c" in "/">
#di_subroutine_type = #llvm.di_subroutine_type<>
#loc4 = loc("l.c":3:3)
#loc5 = loc("l.c":9:1)
#loc6 = loc("l.c":5:3)
#di_compile_unit = #llvm.di_compile_unit<id = distinct[0]<>, sourceLanguage = DW_LANG_C99, file = #di_file, producer = "clang", isOptimized = true, emissionKind = Full>
#di_subprogram = #llvm.di_subprogram<id = distinct[1]<>, compileUnit = #di_compile_unit, scope = #di_file, name = "caller", file = #di_file, line = 1, scopeLine = 1, subprogramFlags = Definition, type = #di_subroutine_type>
#di_subprogram1 = #llvm.di_subprogram<id = distinct[2]<>, compileUnit = #di_compile_unit, scope = #di_file, name = "inlinee", file = #di_file, line = 8, scopeLine = 8, subprogramFlags = Definition, type = #di_subroutine_type>
#loc8 = loc(fused<#di_subprogram1>[#loc4])
#loc9 = loc(fused<#di_subprogram>[#loc5])
#loc10 = loc(fused<#di_subprogram1>[#loc6])
#loc11 = loc(callsite(#loc8 at #loc9))
#loc12 = loc(callsite(#loc10 at #loc9))
#loop_annotation = #llvm.loop_annotation<disableNonforced = true, startLoc = #loc11, endLoc = #loc12>
module {
  llvm.module_flags [#llvm.mlir.module_flag<warning, "Debug Info Version", 3 : i32>] loc(#loc)
  llvm.func @inlined_loop() {
    llvm.br ^bb1 loc(#loc3)
  ^bb1:  // 2 preds: ^bb0, ^bb1
    llvm.br ^bb1 {loop_annotation = #loop_annotation} loc(#loc11)
  } loc(#loc7)
} loc(#loc)
#loc = loc("l.c":0:0)
#loc1 = loc("caller")
#loc2 = loc("l.c":1:0)
#loc3 = loc(unknown)
#loc7 = loc(fused<#di_subprogram>[#loc1, #loc2])
