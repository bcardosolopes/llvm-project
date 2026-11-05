// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global typedef-to-const-array debug type metadata.
// Direct-CIR now preserves the const-qualified element type under the array
// typedef chain.

typedef const int CA[2];
CA gca = {1, 2};

// CHECK-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_const_type, baseType: !{{[0-9]+}})
// CHECK-DAG: !DICompositeType(tag: DW_TAG_array_type, baseType: !{{[0-9]+}}, size: 64, elements: !{{[0-9]+}})
// CHECK-DAG: !DISubrange(count: 2)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "CA", file: {{.*}}, line: 12, baseType: !{{[0-9]+}})
// CHECK-DAG: !DIGlobalVariable(name: "gca", scope: {{.*}}, file: {{.*}}, line: 13, type: !{{[0-9]+}}
