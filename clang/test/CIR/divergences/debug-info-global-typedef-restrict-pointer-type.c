// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
//
//
// Former divergence: global typedef-to-restrict-pointer debug type metadata.
// Direct-CIR now preserves restrict-qualified pointer metadata matching OGCG.

typedef int *__restrict rpint;
int x;
rpint grp = &x;

// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "rpint", file: {{.*}}, line: {{[0-9]+}}, baseType: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_restrict_type, baseType: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !{{[0-9]+}}, size: 64)
// CHECK-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DIGlobalVariable(name: "grp", scope: {{.*}}, file: {{.*}}, line: {{[0-9]+}}, type: !{{[0-9]+}}
// CHECK-DAG: !DIGlobalVariable(name: "x", scope: {{.*}}, file: {{.*}}, line: {{[0-9]+}}, type: !{{[0-9]+}}
