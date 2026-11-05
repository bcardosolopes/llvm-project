// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s

// Regression coverage: keep typedef wrappers when rebuilding enum
// debug info for globals.

typedef enum E {
  A = 1,
  B = 2,
} E_t;

E_t ge = B;

// CHECK-DAG: !DICompositeType(tag: DW_TAG_enumeration_type, name: "E"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "E_t"
// CHECK-DAG: !DIGlobalVariable(name: "ge"
