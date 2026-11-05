// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=OGCG

// An enum-typed member of a record that is only referenced by a module-level
// global (no function in the TU) must carry the enum type in its debug info.
// The global record-type debug rewrite previously had no enum case, so such
// members were left with the enum's underlying integer type.

enum Color { Red, Green = 5, Blue };

struct S {
  int x;
  enum Color c;
};

struct S gs;

// The member c references the enumeration type, not its underlying int.
// CHECK: !DIDerivedType(tag: DW_TAG_member, name: "c"{{.*}}baseType: ![[ENUM:[0-9]+]]
// CHECK: ![[ENUM]] = !DICompositeType(tag: DW_TAG_enumeration_type, name: "Color"
// CHECK-SAME: elements:
// CHECK: !DIEnumerator(name: "Green", value: 5

// OGCG-DAG: !DICompositeType(tag: DW_TAG_enumeration_type, name: "Color"
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "c"
