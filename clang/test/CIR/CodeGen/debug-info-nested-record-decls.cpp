// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

struct C {
  static int s;
  int m(int);
};

int C::s = 0;
int C::m(int x) { return x; }

// The owner record has no instance fields, so its element list should contain
// only the static-member declaration and the declaration-form method DIE.

// CIR-DAG: ![[INT:[0-9]+]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CIR-DAG: ![[REC:[0-9]+]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "C", {{.*}}elements: ![[ELEMS:[0-9]+]]
// CIR-DAG: ![[ELEMS]] = !{![[METHOD_DECL:[0-9]+]], ![[STATIC_DECL:[0-9]+]]}
// CIR-DAG: ![[STATIC_DECL]] = !DIDerivedType(tag: DW_TAG_member, name: "s", scope: ![[REC]], {{.*}}baseType: ![[INT]], flags: DIFlagStaticMember)
// CIR-DAG: ![[METHOD_DECL]] = !DISubprogram(name: "m", linkageName: "_ZN1C1mEi", scope: ![[REC]], {{.*}}spFlags: 0)
// CIR-DAG: ![[METHOD_DEF:[0-9]+]] = distinct !DISubprogram(name: "m", linkageName: "_ZN1C1mEi", scope: ![[REC]], {{.*}}declaration: ![[METHOD_DECL]]
// CIR-DAG: ![[STATIC_VAR:[0-9]+]] = distinct !DIGlobalVariable(name: "s", linkageName: "_ZN1C1sE", {{.*}}declaration: ![[STATIC_DECL]])

// OGCG-DAG: ![[INT:[0-9]+]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// OGCG-DAG: ![[REC:[0-9]+]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "C", {{.*}}elements: ![[ELEMS:[0-9]+]]
// OGCG-DAG: ![[ELEMS]] = !{![[STATIC_DECL:[0-9]+]], ![[METHOD_DECL:[0-9]+]]}
// OGCG-DAG: ![[STATIC_DECL]] = !DIDerivedType(tag: DW_TAG_member, name: "s", scope: ![[REC]], {{.*}}baseType: ![[INT]], flags: DIFlagStaticMember)
// OGCG-DAG: ![[METHOD_DECL]] = !DISubprogram(name: "m", linkageName: "_ZN1C1mEi", scope: ![[REC]], {{.*}}spFlags: 0)
// OGCG-DAG: ![[METHOD_DEF:[0-9]+]] = distinct !DISubprogram(name: "m", linkageName: "_ZN1C1mEi", scope: ![[REC]], {{.*}}declaration: ![[METHOD_DECL]]
// OGCG-DAG: ![[STATIC_VAR:[0-9]+]] = distinct !DIGlobalVariable(name: "s", linkageName: "_ZN1C1sE", {{.*}}declaration: ![[STATIC_DECL]])
