// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited -std=c++17 %s -o - | FileCheck %s --check-prefix=OGCG

// References lower to pointers in CIR, so a reference-typed global variable was
// described in debug info as a DW_TAG_pointer_type (and the referent's
// cv-qualifiers were dropped). Classic CodeGen describes a reference as
// DW_TAG_reference_type (or rvalue_reference_type for `&&`) preserving the
// referent's const/volatile. Verify each form matches.

int gi = 5;
int &gr = gi;
const int &gcr = gi;
int &&grr = 7;

// CHECK-DAG: !DIGlobalVariable(name: "gr",{{.*}}type: ![[LREF:[0-9]+]]
// CHECK-DAG: ![[LREF]] = !DIDerivedType(tag: DW_TAG_reference_type, baseType: ![[INT:[0-9]+]], size: 64)
// CHECK-DAG: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DIGlobalVariable(name: "gcr",{{.*}}type: ![[CREF:[0-9]+]]
// CHECK-DAG: ![[CREF]] = !DIDerivedType(tag: DW_TAG_reference_type, baseType: ![[CONST:[0-9]+]], size: 64)
// CHECK-DAG: ![[CONST]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[INT]])
// CHECK-DAG: !DIGlobalVariable(name: "grr",{{.*}}type: ![[RREF:[0-9]+]]
// CHECK-DAG: ![[RREF]] = !DIDerivedType(tag: DW_TAG_rvalue_reference_type, baseType: ![[INT]], size: 64)

// OGCG-DAG: !DIGlobalVariable(name: "gr",{{.*}}type: ![[OLREF:[0-9]+]]
// OGCG-DAG: ![[OLREF]] = !DIDerivedType(tag: DW_TAG_reference_type, baseType: ![[OINT:[0-9]+]], size: 64)
// OGCG-DAG: ![[OINT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// OGCG-DAG: !DIGlobalVariable(name: "gcr",{{.*}}type: ![[OCREF:[0-9]+]]
// OGCG-DAG: ![[OCREF]] = !DIDerivedType(tag: DW_TAG_reference_type, baseType: ![[OCONST:[0-9]+]], size: 64)
// OGCG-DAG: ![[OCONST]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[OINT]])
// OGCG-DAG: !DIGlobalVariable(name: "grr",{{.*}}type: ![[ORREF:[0-9]+]]
// OGCG-DAG: ![[ORREF]] = !DIDerivedType(tag: DW_TAG_rvalue_reference_type, baseType: ![[OINT]], size: 64)
