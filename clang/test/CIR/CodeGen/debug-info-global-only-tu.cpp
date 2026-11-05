// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR

// Global-only debug TUs still need a DICompileUnit anchored in !llvm.dbg.cu so
// their DIGlobalVariable metadata remains reachable to consumers.

namespace n {
int g = 1;
}

// OGCG: @_ZN1n1gE = global i32 1, align 4, !dbg ![[GVE:[0-9]+]]
// OGCG: !llvm.dbg.cu = !{![[CU:[0-9]+]]}
// OGCG: ![[GVE]] = !DIGlobalVariableExpression(var: ![[GVAR:[0-9]+]], expr: !DIExpression())
// OGCG: ![[GVAR]] = distinct !DIGlobalVariable(name: "g", linkageName: "_ZN1n1gE", scope: ![[NS:[0-9]+]], file: ![[FILE:[0-9]+]], line: {{[0-9]+}}, type: ![[INT:[0-9]+]], isLocal: false, isDefinition: true)
// OGCG: ![[NS]] = !DINamespace(name: "n", scope: null)
// OGCG: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// OGCG: ![[CU]] = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: ![[CUFILE:[0-9]+]], producer: {{.*}}, isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, globals: ![[GLOBALS:[0-9]+]], splitDebugInlining: false, nameTableKind: None)
// OGCG: ![[GLOBALS]] = !{![[GVE]]}

// CIR: @_ZN1n1gE = global i32 1, align 4, !dbg ![[GVE:[0-9]+]]
// CIR: !llvm.dbg.cu = !{![[CU:[0-9]+]]}
// CIR: ![[GVE]] = !DIGlobalVariableExpression(var: ![[GVAR:[0-9]+]], expr: !DIExpression())
// CIR: ![[GVAR]] = distinct !DIGlobalVariable(name: "g", linkageName: "_ZN1n1gE", scope: ![[NS:[0-9]+]], file: ![[FILE:[0-9]+]], line: {{[0-9]+}}, type: ![[INT:[0-9]+]], isLocal: false, isDefinition: true)
// CIR: ![[NS]] = !DINamespace(name: "n", scope: null)
// CIR: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CIR: ![[CU]] = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: ![[CUFILE:[0-9]+]], producer: {{.*}}, isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, globals: ![[GLOBALS:[0-9]+]], splitDebugInlining: false, nameTableKind: None)
// CIR: ![[GLOBALS]] = !{![[GVE]]}
