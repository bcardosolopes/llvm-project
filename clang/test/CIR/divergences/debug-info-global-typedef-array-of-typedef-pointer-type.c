// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global typedef array-of-typedef-pointer debug type
// metadata. Direct-CIR now preserves nested typedef chain matching OGCG.

typedef int *PI;
typedef PI ARRPI[3];
ARRPI garrpi;

// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "ARRPI", file: {{.*}}, line: {{[0-9]+}}, baseType: !{{[0-9]+}})
// CHECK-DAG: !DICompositeType(tag: DW_TAG_array_type, baseType: !{{[0-9]+}}, size: 192, elements: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "PI", file: {{.*}}, line: {{[0-9]+}}, baseType: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !{{[0-9]+}}, size: 64)
// CHECK-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DISubrange(count: 3)
// CHECK-DAG: !DIGlobalVariable(name: "garrpi", scope: {{.*}}, file: {{.*}}, line: {{[0-9]+}}, type: !{{[0-9]+}}
