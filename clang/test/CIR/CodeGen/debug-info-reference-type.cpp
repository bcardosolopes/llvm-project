// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -std=c++17 -o - %s | FileCheck %s
// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux -emit-llvm -debug-info-kind=limited -std=c++17 -o - %s | llvm-as -o /dev/null

// Verify that C++ lvalue and rvalue reference types produce
// DW_TAG_reference_type / DW_TAG_rvalue_reference_type in debug info,
// not DW_TAG_const_type(DW_TAG_pointer_type(...)).

void test_lvalue_ref() {
  int x = 42;
  int &ref = x;
}

void test_rvalue_ref() {
  int &&rref = 42;
}

enum class Status { OK, Error };

void test_enum_ref() {
  Status s = Status::OK;
  Status &ref = s;
}

struct S { int a; };

void test_struct_ref() {
  S s{};
  S &ref = s;
}

// Lvalue reference to int.
// CHECK-DAG: !DILocalVariable(name: "ref", {{.*}}type: ![[LREF_TYPE:[0-9]+]])
// CHECK-DAG: ![[LREF_TYPE]] = !DIDerivedType(tag: DW_TAG_reference_type, baseType: ![[INT:[0-9]+]], size: 64)
// CHECK-DAG: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)

// Rvalue reference to int.
// CHECK-DAG: !DILocalVariable(name: "rref", {{.*}}type: ![[RREF_TYPE:[0-9]+]])
// CHECK-DAG: ![[RREF_TYPE]] = !DIDerivedType(tag: DW_TAG_rvalue_reference_type, baseType: ![[INT]], size: 64)

// Lvalue reference to scoped enum.
// CHECK-DAG: !DILocalVariable(name: "ref", {{.*}}scope: ![[ENUM_FN:[0-9]+]], {{.*}}type: ![[EREF_TYPE:[0-9]+]])
// CHECK-DAG: ![[ENUM_FN]] = distinct !DISubprogram(name: "test_enum_ref",
// CHECK-DAG: ![[EREF_TYPE]] = !DIDerivedType(tag: DW_TAG_reference_type, baseType: ![[STATUS:[0-9]+]], size: 64)
// CHECK-DAG: ![[STATUS]] = !DICompositeType(tag: DW_TAG_enumeration_type, name: "Status",

// Lvalue reference to struct.
// CHECK-DAG: !DILocalVariable(name: "ref", {{.*}}scope: ![[STRUCT_FN:[0-9]+]], {{.*}}type: ![[SREF_TYPE:[0-9]+]])
// CHECK-DAG: ![[STRUCT_FN]] = distinct !DISubprogram(name: "test_struct_ref",
// CHECK-DAG: ![[SREF_TYPE]] = !DIDerivedType(tag: DW_TAG_reference_type, baseType: ![[S_TYPE:[0-9]+]], size: 64)
// CHECK-DAG: ![[S_TYPE]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "S",
