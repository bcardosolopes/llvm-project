// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global typedef complex-float debug type metadata.
// Direct-CIR now preserves complex type as DIBasicType matching OGCG.

typedef _Complex float CF;
CF gcf;

// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "CF", file: {{.*}}, line: {{[0-9]+}}, baseType: !{{[0-9]+}})
// CHECK-DAG: !DIBasicType(name: "complex", size: 64, encoding: DW_ATE_complex_float)
// CHECK-DAG: !DIGlobalVariable(name: "gcf", scope: {{.*}}, file: {{.*}}, line: {{[0-9]+}}, type: !{{[0-9]+}}
