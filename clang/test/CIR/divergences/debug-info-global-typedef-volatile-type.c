// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global typedef volatile debug type metadata.
// Direct-CIR now preserves volatile DIDerivedType under typedef matching OGCG.

typedef volatile int vint;
vint gv;

// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "vint", file: {{.*}}, line: {{[0-9]+}}, baseType: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !{{[0-9]+}})
// CHECK-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DIGlobalVariable(name: "gv", scope: {{.*}}, file: {{.*}}, line: {{[0-9]+}}, type: !{{[0-9]+}}
