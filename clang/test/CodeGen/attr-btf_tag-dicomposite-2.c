// REQUIRES: x86-registered-target
// RUN: %clang -target x86_64 -g -S -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG
// RUN: %if cir-support %{ %clang -target x86_64 -g -S -emit-llvm -o - -fclangir %s | FileCheck %s --check-prefix=CIR %}

#define __tag1 __attribute__((btf_decl_tag("tag1")))
#define __tag2 __attribute__((btf_decl_tag("tag2")))

struct __tag1 __tag2 t1;

int foo(struct t1 *arg) {
  return (int)(long)arg;
}

// OGCG: define dso_local i32 @foo(
// OGCG-NOT: annotations
// OGCG: ![[T1:[0-9]+]] = !DICompositeType(tag: DW_TAG_structure_type, name: "t1", file: ![[#]], line: 8, flags: DIFlagFwdDecl)

// CIR: define dso_local i32 @foo(
// CIR-NOT: annotations
// CIR: ![[T1:[0-9]+]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "t1", file: ![[#]], line: 8, flags: DIFlagFwdDecl)
