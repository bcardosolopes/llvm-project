// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global typedef-to-array debug type metadata. Direct-CIR
// now preserves the typedef over DICompositeType(DW_TAG_array_type).

typedef int A3[3];
A3 ga = {1, 2, 3};

// CHECK-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DICompositeType(tag: DW_TAG_array_type, baseType: !{{[0-9]+}}, size: 96, elements: !{{[0-9]+}})
// CHECK-DAG: !DISubrange(count: 3)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "A3", file: {{.*}}, line: 11, baseType: !{{[0-9]+}})
// CHECK-DAG: !DIGlobalVariable(name: "ga", scope: {{.*}}, file: {{.*}}, line: 12, type: !{{[0-9]+}}
