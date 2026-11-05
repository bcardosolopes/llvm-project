// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

enum E { A = 1, B = 2 };
enum E ge = B;

// CIR-DAG: !DICompositeType(tag: DW_TAG_enumeration_type, name: "E", {{.*}}baseType: ![[UBASE:[0-9]+]], size: 32, elements: ![[ENUMS:[0-9]+]])
// CIR-DAG: ![[UBASE]] = !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
// CIR-DAG: ![[ENUMS]] = !{![[EA:[0-9]+]], ![[EB:[0-9]+]]}
// CIR-DAG: ![[EA]] = !DIEnumerator(name: "A", value: 1)
// CIR-DAG: ![[EB]] = !DIEnumerator(name: "B", value: 2)

// OGCG-DAG: !DICompositeType(tag: DW_TAG_enumeration_type, name: "E", {{.*}}baseType: ![[UBASE:[0-9]+]], size: 32, elements: ![[ENUMS:[0-9]+]])
// OGCG-DAG: ![[UBASE]] = !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
// OGCG-DAG: ![[ENUMS]] = !{![[EA:[0-9]+]], ![[EB:[0-9]+]]}
// OGCG-DAG: ![[EA]] = !DIEnumerator(name: "A", value: 1)
// OGCG-DAG: ![[EB]] = !DIEnumerator(name: "B", value: 2)
