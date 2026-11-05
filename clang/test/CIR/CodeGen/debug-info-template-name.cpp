// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited -std=c++17 %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited -std=c++17 %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A class template specialization's debug-info name must be the full template-id
// (with template arguments) so distinct instantiations are distinguishable in a
// debugger, rather than the bare template name. The instantiations' member
// functions are emitted here, exercising the source-shaped record debug path.

template <typename T, int N> struct Array {
  T data[N];
  T sum() {
    T s = 0;
    for (int i = 0; i < N; ++i)
      s += data[i];
    return s;
  }
};
Array<int, 4> gi;
Array<double, 2> gd;
int use() { return gi.sum() + (int)gd.sum(); }

// CIR-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Array<int, 4>"
// CIR-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Array<double, 2>"

// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Array<int, 4>"
// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Array<double, 2>"
