// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CHECK
//
//
//
// Former divergence: CIR used to drop the volatile wrapper from global debug
// types.

volatile int vg = 7;

// CHECK: ![[GV:[0-9]+]] = distinct !DIGlobalVariable(name: "vg", scope: ![[CU:[0-9]+]], file: ![[FILE:[0-9]+]], line: {{[0-9]+}}, type: ![[VOL:[0-9]+]], isLocal: false, isDefinition: true)
// CHECK: ![[VOL]] = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: ![[INT:[0-9]+]])
// CHECK: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
