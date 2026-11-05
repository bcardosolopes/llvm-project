// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global typedef-to-enum debug type metadata. Direct-CIR
// now preserves the enumeration type and enumerators under the typedef chain.

typedef enum E {
  A = 1,
  B = 2,
} E_t;

E_t ge = B;

// CHECK-DAG: !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
// CHECK-DAG: !DIEnumerator(name: "A", value: 1)
// CHECK-DAG: !DIEnumerator(name: "B", value: 2)
// CHECK-DAG: !DICompositeType(tag: DW_TAG_enumeration_type, name: "E", file: {{.*}}, line: 11, baseType: !{{[0-9]+}}, size: 32, elements: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "E_t", file: {{.*}}, line: 14, baseType: !{{[0-9]+}})
// CHECK-DAG: !DIGlobalVariable(name: "ge", scope: {{.*}}, file: {{.*}}, line: 16, type: !{{[0-9]+}}
