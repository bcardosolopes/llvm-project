// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -std=c++17 -o - %s | FileCheck %s
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -std=c++17 -o - %s | llvm-as -o /dev/null

// Verify that enum types in debug info are correctly represented as
// DICompositeType(DW_TAG_enumeration_type) rather than DIBasicType(int),
// including through typedef wrappers.

enum class Color { Red, Green, Blue };
typedef Color ColorType;

void test_enum_local() {
  Color c = Color::Red;
}

void test_enum_typedef() {
  ColorType ct = ColorType::Green;
}

// Direct enum local should have a DW_TAG_enumeration_type, not DIBasicType.
// CHECK-DAG: !DILocalVariable(name: "c", {{.*}}type: ![[C_TYPE:[0-9]+]])
// CHECK-DAG: ![[C_TYPE]] = !DICompositeType(tag: DW_TAG_enumeration_type, name: "Color",

// Typedef-to-enum local should have DW_TAG_typedef wrapping the enum type.
// CHECK-DAG: !DILocalVariable(name: "ct", {{.*}}type: ![[CT_TYPE:[0-9]+]])
// CHECK-DAG: ![[CT_TYPE]] = !DIDerivedType(tag: DW_TAG_typedef, name: "ColorType",

// Enum enumerators should be present.
// CHECK-DAG: !DIEnumerator(name: "Red", value: 0)
// CHECK-DAG: !DIEnumerator(name: "Green", value: 1)
// CHECK-DAG: !DIEnumerator(name: "Blue", value: 2)
