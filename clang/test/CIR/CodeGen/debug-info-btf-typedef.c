// REQUIRES: x86-registered-target
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

#define __tag1 __attribute__((btf_decl_tag("tag1")))

typedef struct { int a; } __s __tag1;
typedef unsigned * __u __tag1;
__s a;
__u u;

// CIR-DAG: ![[ANNOT:[0-9]+]] = !{![[TAG1:[0-9]+]]}
// CIR-DAG: ![[TAG1]] = !{!"btf_decl_tag", !"tag1"}
// CIR-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "__u", file: ![[#]], line: [[#]], baseType: ![[#]], annotations: ![[ANNOT]])
// CIR-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "__s", file: ![[#]], line: [[#]], baseType: ![[#]], annotations: ![[ANNOT]])

// OGCG-DAG: ![[ANNOT:[0-9]+]] = !{![[TAG1:[0-9]+]]}
// OGCG-DAG: ![[TAG1]] = !{!"btf_decl_tag", !"tag1"}
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "__u", file: ![[#]], line: [[#]], baseType: ![[#]], annotations: ![[ANNOT]])
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "__s", file: ![[#]], line: [[#]], baseType: ![[#]], annotations: ![[ANNOT]])
