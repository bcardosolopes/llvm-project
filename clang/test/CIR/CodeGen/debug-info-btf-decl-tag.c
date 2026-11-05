// REQUIRES: x86-registered-target
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=CIR

#define __tag1 __attribute__((btf_decl_tag("tag1")))
#define __tag2 __attribute__((btf_decl_tag("tag2")))

struct __tag1 __tag2 Tagged;
struct Tagged {
  int a;
};

struct Tagged g __tag1 __tag2;

int __tag1 __tag2 foo(struct Tagged *arg) {
  return arg->a;
}

// CIR-DAG: !DIGlobalVariable(name: "g", {{.*}}annotations: ![[ANN:[0-9]+]])
// CIR-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "Tagged", {{.*}}annotations: ![[ANN]])
// CIR-DAG: !DISubprogram(name: "foo", {{.*}}annotations: ![[ANN]])
// CIR-DAG: ![[ANN]] = !{![[TAG1:[0-9]+]], ![[TAG2:[0-9]+]]}
// CIR-DAG: ![[TAG1]] = !{!"btf_decl_tag", !"tag1"}
// CIR-DAG: ![[TAG2]] = !{!"btf_decl_tag", !"tag2"}
