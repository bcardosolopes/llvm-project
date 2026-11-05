// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that CIR emits DIGlobalVariable debug info for global variables,
// matching OGCG output.

int g = 42;

// CIR-DAG: @g = global i32 42, align 4, !dbg ![[GV_EXPR:[0-9]+]]
// CIR-DAG: ![[GV_EXPR]] = !DIGlobalVariableExpression(var: ![[GV:[0-9]+]], expr: !DIExpression())
// CIR-DAG: ![[GV]] = distinct !DIGlobalVariable(name: "g", scope: ![[CU:[0-9]+]], file: ![[FILE:[0-9]+]], line: 11, type: ![[INT:[0-9]+]], isLocal: false, isDefinition: true)
// CIR-DAG: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CIR-DAG: ![[CU]] = distinct !DICompileUnit(

// OGCG-DAG: @g = global i32 42, align 4, !dbg ![[GV_EXPR:[0-9]+]]
// OGCG-DAG: ![[GV_EXPR]] = !DIGlobalVariableExpression(var: ![[GV:[0-9]+]], expr: !DIExpression())
// OGCG-DAG: ![[GV]] = distinct !DIGlobalVariable(name: "g", scope: ![[CU:[0-9]+]], file: ![[FILE:[0-9]+]], line: 11, type: ![[INT:[0-9]+]], isLocal: false, isDefinition: true)
// OGCG-DAG: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// OGCG-DAG: ![[CU]] = distinct !DICompileUnit(

double d = 3.14;

// CIR-DAG: @d = global double 3.14{{[0-9e+]*}}, align 8, !dbg ![[DV_EXPR:[0-9]+]]
// CIR-DAG: ![[DV_EXPR]] = !DIGlobalVariableExpression(var: ![[DV:[0-9]+]], expr: !DIExpression())
// CIR-DAG: ![[DV]] = distinct !DIGlobalVariable(name: "d", {{.*}}type: ![[DOUBLE:[0-9]+]], isLocal: false, isDefinition: true)
// CIR-DAG: ![[DOUBLE]] = !DIBasicType(name: "double", size: 64, encoding: DW_ATE_float)

// OGCG-DAG: @d = global double 3.14{{[0-9e+]*}}, align 8, !dbg ![[DV_EXPR:[0-9]+]]
// OGCG-DAG: ![[DV_EXPR]] = !DIGlobalVariableExpression(var: ![[DV:[0-9]+]], expr: !DIExpression())
// OGCG-DAG: ![[DV]] = distinct !DIGlobalVariable(name: "d", {{.*}}type: ![[DOUBLE:[0-9]+]], isLocal: false, isDefinition: true)
// OGCG-DAG: ![[DOUBLE]] = !DIBasicType(name: "double", size: 64, encoding: DW_ATE_float)

static int s = 10;
int use_s(void) { return s; }

// CIR-DAG: !DIGlobalVariable(name: "s", {{.*}}isLocal: true, isDefinition: true)

// OGCG-DAG: !DIGlobalVariable(name: "s", {{.*}}isLocal: true, isDefinition: true)

void dummy(void) {}
