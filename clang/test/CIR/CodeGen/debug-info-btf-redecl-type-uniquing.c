// REQUIRES: x86-registered-target
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.og.o0.ll
// RUN: FileCheck --input-file=%t.og.o0.ll %s --check-prefix=O0
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.o0.ll
// RUN: FileCheck --input-file=%t.cir.o0.ll %s --check-prefix=O0
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.og.o2.ll
// RUN: FileCheck --input-file=%t.og.o2.ll %s --check-prefix=O2
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.o2.ll
// RUN: FileCheck --input-file=%t.cir.o2.ll %s --check-prefix=O2
//
//
//
// Direct-CIR used to rebuild the same source-shaped `struct t1 * -> int`
// debug type chain once per definition in the BTF-tagged redeclaration path,
// while OGCG reused one chain module-wide.

#define __tag1 __attribute__((btf_decl_tag("tag1")))
#define __tag2 __attribute__((btf_decl_tag("tag2")))

struct t1 {
  int a;
};

int __tag1 __tag2 foo(struct t1 *arg) {
  return arg->a;
}

int __tag1 __tag2 foo2(struct t1 *arg);
int foo2(struct t1 *arg) {
  return arg->a;
}

int __tag1 foo3(struct t1 *arg);
int __tag2 foo3(struct t1 *arg) {
  return arg->a;
}

int __tag1 foo4(struct t1 *arg);
int __tag2 foo4(struct t1 *arg);
int foo4(struct t1 *arg) {
  return arg->a;
}

// O0-DAG: ![[SUB:[0-9]+]] = !DISubroutineType(types: ![[SUBTYPES:[0-9]+]])
// O0-DAG: ![[SUBTYPES]] = !{![[INT:[0-9]+]], ![[PTR:[0-9]+]]}
// O0-DAG: ![[PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[T1:[0-9]+]], size: 64)
// O0-DAG: ![[T1]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "t1"
// O0-DAG: ![[FOO:[0-9]+]] = distinct !DISubprogram(name: "foo"
// O0-DAG-SAME: type: ![[SUB]]
// O0-DAG: ![[ARG0:[0-9]+]] = !DILocalVariable(name: "arg", arg: 1, scope: ![[FOO]], file: ![[#]], line: [[#]], type: ![[PTR]])
// O0-DAG: ![[FOO2:[0-9]+]] = distinct !DISubprogram(name: "foo2"
// O0-DAG-SAME: type: ![[SUB]]
// O0-DAG: ![[ARG1:[0-9]+]] = !DILocalVariable(name: "arg", arg: 1, scope: ![[FOO2]], file: ![[#]], line: [[#]], type: ![[PTR]])
// O0-DAG: ![[FOO3:[0-9]+]] = distinct !DISubprogram(name: "foo3"
// O0-DAG-SAME: type: ![[SUB]]
// O0-DAG: ![[ARG2:[0-9]+]] = !DILocalVariable(name: "arg", arg: 1, scope: ![[FOO3]], file: ![[#]], line: [[#]], type: ![[PTR]])
// O0-DAG: ![[FOO4:[0-9]+]] = distinct !DISubprogram(name: "foo4"
// O0-DAG-SAME: type: ![[SUB]]
// O0-DAG: ![[ARG3:[0-9]+]] = !DILocalVariable(name: "arg", arg: 1, scope: ![[FOO4]], file: ![[#]], line: [[#]], type: ![[PTR]])

// O2-DAG: ![[SUB:[0-9]+]] = !DISubroutineType(types: ![[SUBTYPES:[0-9]+]])
// O2-DAG: ![[SUBTYPES]] = !{![[INT:[0-9]+]], ![[PTR:[0-9]+]]}
// O2-DAG: ![[PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[T1:[0-9]+]], size: 64)
// O2-DAG: ![[T1]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "t1"
// O2-DAG: ![[FOO:[0-9]+]] = distinct !DISubprogram(name: "foo"
// O2-DAG-SAME: type: ![[SUB]]
// O2-DAG: ![[ARG0:[0-9]+]] = !DILocalVariable(name: "arg", arg: 1, scope: ![[FOO]], file: ![[#]], line: [[#]], type: ![[PTR]])
// O2-DAG: ![[FOO2:[0-9]+]] = distinct !DISubprogram(name: "foo2"
// O2-DAG-SAME: type: ![[SUB]]
// O2-DAG: ![[ARG1:[0-9]+]] = !DILocalVariable(name: "arg", arg: 1, scope: ![[FOO2]], file: ![[#]], line: [[#]], type: ![[PTR]])
// O2-DAG: ![[FOO3:[0-9]+]] = distinct !DISubprogram(name: "foo3"
// O2-DAG-SAME: type: ![[SUB]]
// O2-DAG: ![[ARG2:[0-9]+]] = !DILocalVariable(name: "arg", arg: 1, scope: ![[FOO3]], file: ![[#]], line: [[#]], type: ![[PTR]])
// O2-DAG: ![[FOO4:[0-9]+]] = distinct !DISubprogram(name: "foo4"
// O2-DAG-SAME: type: ![[SUB]]
// O2-DAG: ![[ARG3:[0-9]+]] = !DILocalVariable(name: "arg", arg: 1, scope: ![[FOO4]], file: ![[#]], line: [[#]], type: ![[PTR]])
