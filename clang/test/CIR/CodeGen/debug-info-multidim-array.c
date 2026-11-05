// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A multidimensional array must be described by a single DW_TAG_array_type with
// one subrange per dimension (the canonical DWARF shape, matching classic
// CodeGen), not as nested array types (one per dimension). CIR lowers a C
// multidim array to nested cir.array types; the debug-info construction now
// collapses them back into a single array type with multiple subranges.

int use(void) {
  int a[2][3][4];
  a[1][2][3] = 7;
  return a[1][2][3];
}

// CIR-DAG: !DICompositeType(tag: DW_TAG_array_type, baseType: ![[ELT:[0-9]+]], size: 768, elements: ![[ELEMS:[0-9]+]])
// CIR-DAG: ![[ELT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CIR-DAG: ![[ELEMS]] = !{![[S0:[0-9]+]], ![[S1:[0-9]+]], ![[S2:[0-9]+]]}
// CIR-DAG: ![[S0]] = !DISubrange(count: 2)
// CIR-DAG: ![[S1]] = !DISubrange(count: 3)
// CIR-DAG: ![[S2]] = !DISubrange(count: 4)

// OGCG-DAG: !DICompositeType(tag: DW_TAG_array_type, baseType: ![[OELT:[0-9]+]], size: 768, elements: ![[OELEMS:[0-9]+]])
// OGCG-DAG: ![[OELEMS]] = !{![[OS0:[0-9]+]], ![[OS1:[0-9]+]], ![[OS2:[0-9]+]]}
// OGCG-DAG: ![[OS0]] = !DISubrange(count: 2)
// OGCG-DAG: ![[OS1]] = !DISubrange(count: 3)
// OGCG-DAG: ![[OS2]] = !DISubrange(count: 4)
