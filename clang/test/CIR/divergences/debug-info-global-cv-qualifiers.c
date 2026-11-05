// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global cv-qualifier debug metadata. Direct-CIR now emits
// the same const/volatile DIDerivedType wrappers as OGCG.

const int ci = 1;
volatile int vi = 2;
const volatile int cvi = 3;

// CHECK-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_const_type, baseType: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_const_type, baseType: !{{[0-9]+}})
// CHECK-DAG: !DIGlobalVariable(name: "ci", scope: {{.*}}, file: {{.*}}, line: 11, type: !{{[0-9]+}}
// CHECK-DAG: !DIGlobalVariable(name: "vi", scope: {{.*}}, file: {{.*}}, line: 12, type: !{{[0-9]+}}
// CHECK-DAG: !DIGlobalVariable(name: "cvi", scope: {{.*}}, file: {{.*}}, line: 13, type: !{{[0-9]+}}
