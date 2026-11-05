// REQUIRES: x86-registered-target
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

#define __tag1 __attribute__((btf_decl_tag("tag1")))
#define __tag2 __attribute__((btf_decl_tag("tag2")))

struct t1 {
  int a __tag1 __tag2;
};

int foo(struct t1 *arg) {
  return arg->a;
}

struct t2 {
  int b:1 __tag1 __tag2;
};

int foo2(struct t2 *arg) {
  return arg->b;
}

// CIR: !DIDerivedType(tag: DW_TAG_member, name: "a", scope: ![[#]], file: ![[#]], line: [[#]], baseType: ![[#]], size: 32, annotations: ![[ANNOT:[0-9]+]])
// CIR-DAG: ![[ANNOT]] = !{![[TAG1:[0-9]+]], ![[TAG2:[0-9]+]]}
// CIR-DAG: ![[TAG1]] = !{!"btf_decl_tag", !"tag1"}
// CIR-DAG: ![[TAG2]] = !{!"btf_decl_tag", !"tag2"}
// CIR-DAG: !DIDerivedType(tag: DW_TAG_member, name: "b", scope: ![[#]], file: ![[#]], line: [[#]], baseType: ![[#]], size: 1, flags: DIFlagBitField, extraData: i64 0, annotations: ![[ANNOT]])

// OGCG: !DIDerivedType(tag: DW_TAG_member, name: "a", scope: ![[#]], file: ![[#]], line: [[#]], baseType: ![[#]], size: 32, annotations: ![[ANNOT:[0-9]+]])
// OGCG-DAG: ![[ANNOT]] = !{![[TAG1:[0-9]+]], ![[TAG2:[0-9]+]]}
// OGCG-DAG: ![[TAG1]] = !{!"btf_decl_tag", !"tag1"}
// OGCG-DAG: ![[TAG2]] = !{!"btf_decl_tag", !"tag2"}
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "b", scope: ![[#]], file: ![[#]], line: [[#]], baseType: ![[#]], size: 1, flags: DIFlagBitField, extraData: i64 0, annotations: ![[ANNOT]])
