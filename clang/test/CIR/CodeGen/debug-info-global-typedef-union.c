// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s

// Regression coverage: preserve typedef wrappers while rebuilding
// union debug metadata for globals.

typedef union U {
  int i;
  float f;
} U_t;

U_t gu = {.i = 3};

// CHECK-DAG: !DICompositeType(tag: DW_TAG_union_type, name: "U"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "i"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "f"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "U_t"
