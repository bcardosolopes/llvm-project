// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s

// Regression coverage: preserve typedef wrapper while rebuilding
// struct member debug metadata for global variables.

typedef struct S {
  int x;
} S_t;

S_t gs = {1};

// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "S"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "x"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "S_t"
