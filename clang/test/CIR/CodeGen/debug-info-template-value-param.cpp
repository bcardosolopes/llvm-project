// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited -std=c++17 %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited -std=c++17 %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A function template with a non-type (value) template parameter must emit a
// DW_TAG_template_value_parameter (with its constant value), not drop the whole
// template-parameter list.

template <int N, typename T> T addN(T x) { return x + N; }
int u1() { return addN<5, int>(10); }
double u2() { return addN<3, double>(2.0); }

// CIR-DAG: !DITemplateValueParameter(name: "N", type: !{{[0-9]+}}, value: i32 5)
// CIR-DAG: !DITemplateValueParameter(name: "N", type: !{{[0-9]+}}, value: i32 3)
// CIR-DAG: !DITemplateTypeParameter(name: "T",

// OGCG-DAG: !DITemplateValueParameter(name: "N", type: !{{[0-9]+}}, value: i32 5)
// OGCG-DAG: !DITemplateValueParameter(name: "N", type: !{{[0-9]+}}, value: i32 3)
// OGCG-DAG: !DITemplateTypeParameter(name: "T",
