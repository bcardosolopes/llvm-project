// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CHECK
//
//
//
// Former divergence: CIR used to collapse plain `char` and `signed char`
// through one signed-char debug type instead of keeping distinct names.

char c = 1;
signed char sc = 2;
unsigned char uc = 3;

// CHECK-DAG: ![[C:[0-9]+]] = distinct !DIGlobalVariable(name: "c", scope: ![[CU:[0-9]+]], file: ![[FILE:[0-9]+]], line: {{[0-9]+}}, type: ![[CHAR_TY:[0-9]+]], isLocal: false, isDefinition: true)
// CHECK-DAG: ![[SC:[0-9]+]] = distinct !DIGlobalVariable(name: "sc", scope: ![[CU]], file: ![[FILE]], line: {{[0-9]+}}, type: ![[SCHAR_TY:[0-9]+]], isLocal: false, isDefinition: true)
// CHECK-DAG: ![[UC:[0-9]+]] = distinct !DIGlobalVariable(name: "uc", scope: ![[CU]], file: ![[FILE]], line: {{[0-9]+}}, type: ![[UCHAR_TY:[0-9]+]], isLocal: false, isDefinition: true)
// CHECK-DAG: ![[CHAR_TY]] = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
// CHECK-DAG: ![[SCHAR_TY]] = !DIBasicType(name: "signed char", size: 8, encoding: DW_ATE_signed_char)
// CHECK-DAG: ![[UCHAR_TY]] = !DIBasicType(name: "unsigned char", size: 8, encoding: DW_ATE_unsigned_char)
