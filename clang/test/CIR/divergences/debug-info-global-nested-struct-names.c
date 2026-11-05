// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
//
// Regression coverage.
//
// Former divergence: nested global struct debug metadata. Direct-CIR now
// preserves inner record/member source metadata matching OGCG.

struct O {
  struct I {
    int x;
  } i;
} go = {{7}};

// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "O", file: {{.*}}, line: 11, size: 32, elements: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "i", scope: !{{[0-9]+}}, file: {{.*}}, line: 14, baseType: !{{[0-9]+}}, size: 32)
// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "I", file: {{.*}}, line: 12, size: 32, elements: !{{[0-9]+}})
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "x", scope: !{{[0-9]+}}, file: {{.*}}, line: 13, baseType: !{{[0-9]+}}, size: 32)
// CHECK-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DIGlobalVariable(name: "go", scope: {{.*}}, file: {{.*}}, line: 15, type: !{{[0-9]+}}
