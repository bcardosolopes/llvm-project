// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

int x;
int *gp = &x;
int arr[3] = {1, 2, 3};
int foo(void) { return 0; }
int (*fp)(void) = &foo;

// CIR-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CIR-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[#]], size: 64)
// CIR-DAG: !DISubroutineType({{(cc: DW_CC_normal, )?}}types: ![[SUBTYPES:[0-9]+]])
// CIR-DAG: ![[SUBTYPES]] = !{![[#]]}
// CIR-DAG: !DICompositeType(tag: DW_TAG_array_type, baseType: ![[#]], size: 96, elements: ![[ELEMS:[0-9]+]])
// CIR-DAG: ![[ELEMS]] = !{![[SUBRANGE:[0-9]+]]}
// CIR-DAG: ![[SUBRANGE]] = !DISubrange(count: 3)

// OGCG-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[#]], size: 64)
// OGCG-DAG: !DISubroutineType({{(cc: DW_CC_normal, )?}}types: ![[SUBTYPES:[0-9]+]])
// OGCG-DAG: ![[SUBTYPES]] = !{![[#]]}
// OGCG-DAG: !DICompositeType(tag: DW_TAG_array_type, baseType: ![[#]], size: 96, elements: ![[ELEMS:[0-9]+]])
// OGCG-DAG: ![[ELEMS]] = !{![[SUBRANGE:[0-9]+]]}
// OGCG-DAG: ![[SUBRANGE]] = !DISubrange(count: 3)
