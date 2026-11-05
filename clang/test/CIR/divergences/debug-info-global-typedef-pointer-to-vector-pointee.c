// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global typedef pointer-to-vector debug type metadata.
// Direct-CIR now preserves typedef chain and vector pointee matching OGCG.

typedef int V4 __attribute__((vector_size(16)));
typedef V4 *PV4;
PV4 gpv;

// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "PV4", file: {{.*}}, line: {{[0-9]+}}, baseType: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !{{[0-9]+}}, size: 64)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "V4", file: {{.*}}, line: {{[0-9]+}}, baseType: !{{[0-9]+}})
// CHECK-DAG: !DICompositeType(tag: DW_TAG_array_type, baseType: !{{[0-9]+}}, size: 128, flags: DIFlagVector, elements: !{{[0-9]+}})
// CHECK-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DISubrange(count: 4)
// CHECK-DAG: !DIGlobalVariable(name: "gpv", scope: {{.*}}, file: {{.*}}, line: {{[0-9]+}}, type: !{{[0-9]+}}
