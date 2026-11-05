// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that direct-CIR rebuilds the same nested DINamespace scope chain as
// OGCG for subprograms and source-shaped types reachable from function
// signatures.

namespace outer {
namespace inner {
struct S {
  int x;
};

using T = S;

T f(T t) { return t; }

int g = 1;
} // namespace inner
} // namespace outer

// CIR-DAG: ![[OUTER:[0-9]+]] = !DINamespace(name: "outer", scope: null)
// CIR-DAG: ![[INNER:[0-9]+]] = !DINamespace(name: "inner", scope: ![[OUTER]])
// CIR-DAG: !llvm.dbg.cu = !{![[CU:[0-9]+]]}
// CIR-DAG: ![[CU]] = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: ![[CUFILE:[0-9]+]], producer: {{.*}}, isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, globals: ![[GLOBALS:[0-9]+]], splitDebugInlining: false, nameTableKind: None)
// CIR-DAG: ![[GLOBALS]] = !{![[GVE:[0-9]+]]}
// CIR-DAG: ![[SP:[0-9]+]] = distinct !DISubprogram(name: "f", linkageName: "_ZN5outer5inner1fENS0_1SE", scope: ![[INNER]]
// CIR-DAG: ![[SUBTYS:[0-9]+]] = !{![[SIG_T:[0-9]+]], ![[SIG_T]]}
// CIR-DAG: ![[REC:[0-9]+]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "S", scope: ![[INNER]]
// CIR-DAG: !DIDerivedType(tag: DW_TAG_member, name: "x", scope: ![[REC]]
// CIR-DAG: ![[SIG_T]] = !DIDerivedType(tag: DW_TAG_typedef, name: "T", scope: ![[INNER]], file: ![[#]], line: 18, baseType: ![[REC]])
// CIR-DAG: !DILocalVariable(name: "t", arg: 1, scope: ![[SP]]
// CIR-DAG: ![[GVE]] = !DIGlobalVariableExpression(var: ![[GVAR:[0-9]+]], expr: !DIExpression())
// CIR-DAG: ![[GVAR]] = distinct !DIGlobalVariable(name: "g", linkageName: "_ZN5outer5inner1gE", scope: ![[INNER]], file: ![[#]], line: 22, type: ![[#]], isLocal: false, isDefinition: true)

// OGCG-DAG: ![[OUTER:[0-9]+]] = !DINamespace(name: "outer", scope: null)
// OGCG-DAG: ![[INNER:[0-9]+]] = !DINamespace(name: "inner", scope: ![[OUTER]])
// OGCG-DAG: !llvm.dbg.cu = !{![[CU:[0-9]+]]}
// OGCG-DAG: ![[CU]] = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: ![[CUFILE:[0-9]+]], producer: {{.*}}, isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, globals: ![[GLOBALS:[0-9]+]], splitDebugInlining: false, nameTableKind: None)
// OGCG-DAG: ![[GLOBALS]] = !{![[GVE:[0-9]+]]}
// OGCG-DAG: ![[SP:[0-9]+]] = distinct !DISubprogram(name: "f", linkageName: "_ZN5outer5inner1fENS0_1SE", scope: ![[INNER]]
// OGCG-DAG: ![[SUBTYS:[0-9]+]] = !{![[SIG_T:[0-9]+]], ![[SIG_T]]}
// OGCG-DAG: ![[REC:[0-9]+]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "S", scope: ![[INNER]]
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "x", scope: ![[REC]]
// OGCG-DAG: ![[SIG_T]] = !DIDerivedType(tag: DW_TAG_typedef, name: "T", scope: ![[INNER]], file: ![[#]], line: 18, baseType: ![[REC]])
// OGCG-DAG: !DILocalVariable(name: "t", arg: 1, scope: ![[SP]]
// OGCG-DAG: ![[GVE]] = !DIGlobalVariableExpression(var: ![[GVAR:[0-9]+]], expr: !DIExpression())
// OGCG-DAG: ![[GVAR]] = distinct !DIGlobalVariable(name: "g", linkageName: "_ZN5outer5inner1gE", scope: ![[INNER]], file: ![[#]], line: 22, type: ![[#]], isLocal: false, isDefinition: true)
