// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global union debug type metadata. Direct-CIR now
// preserves union member metadata matching OGCG.

union U {
  int i;
  float f;
};

union U gu = {.i = 7};

// CHECK-DAG: !DICompositeType(tag: DW_TAG_union_type, name: "U", file: {{.*}}, line: 11, size: 32, elements: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "i", scope: !{{[0-9]+}}, file: {{.*}}, line: 12, baseType: !{{[0-9]+}}, size: 32)
// CHECK-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "f", scope: !{{[0-9]+}}, file: {{.*}}, line: 13, baseType: !{{[0-9]+}}, size: 32)
// CHECK-DAG: !DIBasicType(name: "float", size: 32, encoding: DW_ATE_float)
// CHECK-DAG: !DIGlobalVariable(name: "gu", scope: {{.*}}, file: {{.*}}, line: 16, type: !{{[0-9]+}}
