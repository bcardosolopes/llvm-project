// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CHECK
//
//
//
// Former divergence: CIR used to drop the const wrapper under the typedef
// chain for global debug types.

typedef const int cint;
cint gc = 7;

// CHECK: ![[GV:[0-9]+]] = distinct !DIGlobalVariable(name: "gc", scope: ![[CU:[0-9]+]], file: ![[FILE:[0-9]+]], line: {{[0-9]+}}, type: ![[TYPEDEF:[0-9]+]], isLocal: false, isDefinition: true)
// CHECK: ![[TYPEDEF]] = !DIDerivedType(tag: DW_TAG_typedef, name: "cint", file: ![[FILE]], line: {{[0-9]+}}, baseType: ![[CONST:[0-9]+]])
// CHECK: ![[CONST]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[INT:[0-9]+]])
// CHECK: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
