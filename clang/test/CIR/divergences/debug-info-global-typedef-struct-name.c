// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global typedef-to-struct debug type metadata.
// Direct-CIR now preserves typedef over struct DICompositeType matching OGCG.

struct S { int x; };
typedef struct S S_t;
S_t gs;

// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "S_t", file: {{.*}}, line: {{[0-9]+}}, baseType: !{{[0-9]+}})
// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "S", file: {{.*}}, line: {{[0-9]+}}, size: 32, elements: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "x", scope: !{{[0-9]+}}, file: {{.*}}, line: {{[0-9]+}}, baseType: !{{[0-9]+}}, size: 32)
// CHECK-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DIGlobalVariable(name: "gs", scope: {{.*}}, file: {{.*}}, line: {{[0-9]+}}, type: !{{[0-9]+}}
