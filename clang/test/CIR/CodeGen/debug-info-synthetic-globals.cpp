// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR

// direct-CIR should only attach DIGlobalVariable metadata to source globals.
// Compiler-synthesized helpers like C++ guard variables and llvm.global_ctors
// must stay debug-less to match OGCG.

namespace test4 {
int foo();
__attribute__((weak)) int x = foo();
} // namespace test4

// OGCG: @_ZN5test41xE = weak global i32 0, align 4, !dbg ![[XGVE:[0-9]+]]
// OGCG-DAG: ![[NS:[0-9]+]] = !DINamespace(name: "test4", scope: null)
// OGCG-DAG: ![[INT:[0-9]+]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// OGCG-DAG: ![[XGVE]] = !DIGlobalVariableExpression(var: ![[XVAR:[0-9]+]], expr: !DIExpression())
// OGCG-DAG: ![[XVAR]] = distinct !DIGlobalVariable(name: "x", linkageName: "_ZN5test41xE", scope: ![[NS]], file: ![[#]], line: {{[0-9]+}}, type: ![[INT]], isLocal: false, isDefinition: true)
// OGCG-NOT: !DIGlobalVariable(name: "_ZGVN5test41xE"
// OGCG-NOT: !DIGlobalVariable(name: "llvm.global_ctors"

// CIR: @_ZN5test41xE = weak global i32 0, align 4, !dbg ![[XGVE:[0-9]+]]
// CIR-DAG: ![[NS:[0-9]+]] = !DINamespace(name: "test4", scope: null)
// CIR-DAG: ![[INT:[0-9]+]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CIR-DAG: ![[XGVE]] = !DIGlobalVariableExpression(var: ![[XVAR:[0-9]+]], expr: !DIExpression())
// CIR-DAG: ![[XVAR]] = distinct !DIGlobalVariable(name: "x", linkageName: "_ZN5test41xE", scope: ![[NS]], file: ![[#]], line: {{[0-9]+}}, type: ![[INT]], isLocal: false, isDefinition: true)
// CIR-NOT: !DIGlobalVariable(name: "_ZGVN5test41xE"
// CIR-NOT: !DIGlobalVariable(name: "llvm.global_ctors"
