// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Ensure CIR emits float global debug info as DW_ATE_float with 32-bit size,
// matching OGCG behavior.

float f = 1.25f;

// CIR: @f = global float 1.250000e+00, align 4, !dbg ![[EXPR:[0-9]+]]
// CIR: ![[EXPR]] = !DIGlobalVariableExpression(var: ![[GV:[0-9]+]], expr: !DIExpression())
// CIR: ![[GV]] = distinct !DIGlobalVariable(name: "f", {{.*}}type: ![[TY:[0-9]+]], isLocal: false, isDefinition: true)
// CIR: ![[TY]] = !DIBasicType(name: "float", size: 32, encoding: DW_ATE_float)

// OGCG: @f = global float 1.250000e+00, align 4, !dbg ![[EXPR:[0-9]+]]
// OGCG: ![[EXPR]] = !DIGlobalVariableExpression(var: ![[GV:[0-9]+]], expr: !DIExpression())
// OGCG: ![[GV]] = distinct !DIGlobalVariable(name: "f", {{.*}}type: ![[TY:[0-9]+]], isLocal: false, isDefinition: true)
// OGCG: ![[TY]] = !DIBasicType(name: "float", size: 32, encoding: DW_ATE_float)
