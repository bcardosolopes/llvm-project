// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global typedef complex-int debug type metadata.
// Direct-CIR now preserves complex type as DIBasicType matching OGCG.

typedef _Complex int CI;
CI gci;

// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "CI", file: {{.*}}, line: {{[0-9]+}}, baseType: !{{[0-9]+}})
// CHECK-DAG: !DIBasicType(name: "complex", size: 64, encoding: 128)
// CHECK-DAG: !DIGlobalVariable(name: "gci", scope: {{.*}}, file: {{.*}}, line: {{[0-9]+}}, type: !{{[0-9]+}}
