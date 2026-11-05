// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

struct B {
  unsigned a : 3;
  unsigned b : 5;
};

struct O {
  struct I {
    int x;
  } i;
};

struct P {
  int x;
  double y;
};

struct B gb = {1, 2};
struct O go = {{7}};
struct P gp = {3, 4.0};

// CIR-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "B", file: ![[#]], line: [[#]], size: [[#]], elements: ![[B_ELEMS:[0-9]+]])
// CIR-DAG: ![[B_ELEMS]] = !{![[B_A:[0-9]+]], ![[B_B:[0-9]+]]}
// CIR-DAG: ![[B_A]] = !DIDerivedType(tag: DW_TAG_member, name: "a",
// CIR-DAG: ![[B_B]] = !DIDerivedType(tag: DW_TAG_member, name: "b",
// CIR-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "O", file: ![[#]], line: [[#]], size: [[#]], elements: ![[O_ELEMS:[0-9]+]])
// CIR-DAG: ![[O_ELEMS]] = !{![[O_I:[0-9]+]]}
// CIR-DAG: ![[O_I]] = !DIDerivedType(tag: DW_TAG_member, name: "i",
// CIR-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "I", file: ![[#]], line: [[#]], size: [[#]], elements: ![[I_ELEMS:[0-9]+]])
// CIR-DAG: ![[I_ELEMS]] = !{![[I_X:[0-9]+]]}
// CIR-DAG: ![[I_X]] = !DIDerivedType(tag: DW_TAG_member, name: "x",
// CIR-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "P", file: ![[#]], line: [[#]], size: 128, elements: ![[P_ELEMS:[0-9]+]])
// CIR-DAG: ![[P_ELEMS]] = !{![[P_X:[0-9]+]], ![[P_Y:[0-9]+]]}
// CIR-DAG: ![[P_X]] = !DIDerivedType(tag: DW_TAG_member, name: "x",
// CIR-DAG: ![[P_Y]] = !DIDerivedType(tag: DW_TAG_member, name: "y",

// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "B", file: ![[#]], line: [[#]], size: [[#]], elements: ![[B_ELEMS:[0-9]+]])
// OGCG-DAG: ![[B_ELEMS]] = !{![[B_A:[0-9]+]], ![[B_B:[0-9]+]]}
// OGCG-DAG: ![[B_A]] = !DIDerivedType(tag: DW_TAG_member, name: "a",
// OGCG-DAG: ![[B_B]] = !DIDerivedType(tag: DW_TAG_member, name: "b",
// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "O", file: ![[#]], line: [[#]], size: [[#]], elements: ![[O_ELEMS:[0-9]+]])
// OGCG-DAG: ![[O_ELEMS]] = !{![[O_I:[0-9]+]]}
// OGCG-DAG: ![[O_I]] = !DIDerivedType(tag: DW_TAG_member, name: "i",
// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "I", file: ![[#]], line: [[#]], size: [[#]], elements: ![[I_ELEMS:[0-9]+]])
// OGCG-DAG: ![[I_ELEMS]] = !{![[I_X:[0-9]+]]}
// OGCG-DAG: ![[I_X]] = !DIDerivedType(tag: DW_TAG_member, name: "x",
// OGCG-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "P", file: ![[#]], line: [[#]], size: 128, elements: ![[P_ELEMS:[0-9]+]])
// OGCG-DAG: ![[P_ELEMS]] = !{![[P_X:[0-9]+]], ![[P_Y:[0-9]+]]}
// OGCG-DAG: ![[P_X]] = !DIDerivedType(tag: DW_TAG_member, name: "x",
// OGCG-DAG: ![[P_Y]] = !DIDerivedType(tag: DW_TAG_member, name: "y",
