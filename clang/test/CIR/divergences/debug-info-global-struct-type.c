// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
// Regression coverage.
//
// Former divergence: padded global struct debug metadata. Direct-CIR now
// preserves source-shaped debug metadata matching OGCG.

struct S {
  int x;
  double y;
};

struct S g = {1, 2.0};

// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "S", file: {{.*}}, line: 11, size: 128, elements: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "x", scope: !{{[0-9]+}}, file: {{.*}}, line: 12, baseType: !{{[0-9]+}}, size: 32)
// CHECK-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "y", scope: !{{[0-9]+}}, file: {{.*}}, line: 13, baseType: !{{[0-9]+}}, size: 64, offset: 64)
// CHECK-DAG: !DIBasicType(name: "double", size: 64, encoding: DW_ATE_float)
// CHECK-DAG: !DIGlobalVariable(name: "g", scope: {{.*}}, file: {{.*}}, line: 16, type: !{{[0-9]+}}
