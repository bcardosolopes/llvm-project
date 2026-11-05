// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -o - %s | FileCheck %s
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -o - %s | llvm-as -o /dev/null

// Verify that enum types are emitted as retained types on the compile unit
// when only their constants are referenced (not the enum type itself).
// This matches OGCG behavior where DW_TAG_enumeration_type is created
// whenever an EnumConstantDecl is referenced, even if the enum type
// never appears as a variable type.

enum Flags { FLAG_A = 0x1, FLAG_B = 0x2, FLAG_C = 0x4 };

int check_flag(int val) {
  return val & FLAG_B;
}

// The compile unit should have retained types containing the enum.
// CHECK: !DICompileUnit({{.*}}retainedTypes: ![[RETAINED:[0-9]+]]

// CHECK: !DICompositeType(tag: DW_TAG_enumeration_type, name: "Flags", file: ![[FILE:[0-9]+]], line: {{[0-9]+}}, baseType: ![[BASE:[0-9]+]], size: 32, elements: ![[ELEMS:[0-9]+]])
// CHECK: !DIEnumerator(name: "FLAG_A", value: 1)
// CHECK: !DIEnumerator(name: "FLAG_B", value: 2)
// CHECK: !DIEnumerator(name: "FLAG_C", value: 4)
