// RUN: %clang_cc1 -std=gnu++20 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=CHECK
// RUN: %clang_cc1 -std=gnu++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CHECK
//
//
//
// Former divergence: CIR used to lower bool globals as signed char instead of
// a `bool` DW_ATE_boolean debug type.

bool b = true;

// CHECK: ![[GV:[0-9]+]] = distinct !DIGlobalVariable(name: "b", scope: ![[CU:[0-9]+]], file: ![[FILE:[0-9]+]], line: {{[0-9]+}}, type: ![[BOOL:[0-9]+]], isLocal: false, isDefinition: true)
// CHECK: ![[BOOL]] = !DIBasicType(name: "bool", size: 8, encoding: DW_ATE_boolean)
