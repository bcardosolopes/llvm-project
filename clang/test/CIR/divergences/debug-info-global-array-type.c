// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global array debug type. Direct-CIR now emits the same
// DICompositeType(DW_TAG_array_type) with DISubrange metadata as OGCG.

int arr[3] = {1, 2, 3};

// CHECK-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DICompositeType(tag: DW_TAG_array_type, baseType: !{{[0-9]+}}, size: 96, elements: !{{[0-9]+}})
// CHECK-DAG: !DISubrange(count: 3)
// CHECK-DAG: !DIGlobalVariable(name: "arr", scope: {{.*}}, file: {{.*}}, line: 11, type: !{{[0-9]+}}
