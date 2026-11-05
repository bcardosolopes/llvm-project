// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -std=c++17 -o - %s | FileCheck %s
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -std=c++17 -o - %s | llvm-as -o /dev/null

// Verify that arrays of enum types have the correct enum baseType in
// debug info rather than the underlying integer type.

enum class E { X, Y, Z };

void test_array_of_enum() {
  E arr[3] = {E::X, E::Y, E::Z};
}

// CHECK-DAG: !DILocalVariable(name: "arr", {{.*}}type: ![[ARR_TYPE:[0-9]+]])
// CHECK-DAG: ![[ARR_TYPE]] = !DICompositeType(tag: DW_TAG_array_type, baseType: ![[ENUM_TYPE:[0-9]+]], size: 96,
// CHECK-DAG: ![[ENUM_TYPE]] = !DICompositeType(tag: DW_TAG_enumeration_type, name: "E",
// CHECK-DAG: !DIEnumerator(name: "X", value: 0)
// CHECK-DAG: !DIEnumerator(name: "Y", value: 1)
// CHECK-DAG: !DIEnumerator(name: "Z", value: 2)
