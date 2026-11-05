// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global bitfield struct debug type metadata. Direct-CIR now
// preserves bitfield member metadata matching OGCG.

struct B {
  unsigned a : 3;
  unsigned b : 5;
};

struct B gb = {1, 2};

// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "B", file: {{.*}}, line: 11, size: 32, elements: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "a", scope: !{{[0-9]+}}, file: {{.*}}, line: 12, baseType: !{{[0-9]+}}, size: 3, flags: DIFlagBitField, extraData: i64 0)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "b", scope: !{{[0-9]+}}, file: {{.*}}, line: 13, baseType: !{{[0-9]+}}, size: 5, offset: 3, flags: DIFlagBitField, extraData: i64 0)
// CHECK-DAG: !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
// CHECK-DAG: !DIGlobalVariable(name: "gb", scope: {{.*}}, file: {{.*}}, line: 16, type: !{{[0-9]+}}
