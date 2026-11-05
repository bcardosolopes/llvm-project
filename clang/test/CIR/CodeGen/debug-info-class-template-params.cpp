// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=OGCG

// A class template specialization must describe its template parameters in
// debug info (DW_TAG_template_type_parameter / DW_TAG_template_value_parameter).
// CIR-level record debug info carries none, so they are reconstructed at
// lowering time from the AST; previously they were dropped entirely.

template <typename T, int N>
struct Array {
  T data[N];
};

template <typename T>
struct Wrap {
  T value;
};

// Referenced by a function (subprogram record-DI path).
int use(Array<int, 4> *a) { return a->data[0]; }

// Referenced by a global (global record-DI path).
Wrap<double> gw;

// The Array specialization (reached through a function) carries both its type
// and value template parameters; the Wrap specialization (reached through a
// global) carries its type parameter.
// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Array<int, 4>"{{.*}}templateParams:
// CHECK-DAG: !DITemplateTypeParameter(name: "T", type: !{{[0-9]+}})
// CHECK-DAG: !DITemplateValueParameter(name: "N", type: !{{[0-9]+}}, value: i32 4)
// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Wrap{{.*}}templateParams:

// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Array<int, 4>"{{.*}}templateParams:
// OGCG-DAG: !DITemplateValueParameter(name: "N", type: !{{[0-9]+}}, value: i32 4)
