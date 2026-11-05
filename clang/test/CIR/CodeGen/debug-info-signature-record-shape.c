// REQUIRES: x86-registered-target
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

struct S {
  int x;
  int y;
};

int f(struct S *s) {
  return s->x + s->y;
}

// CIR-DAG: ![[SP:[0-9]+]] = distinct !DISubprogram(name: "f", scope: ![[FILE:[0-9]+]], file: ![[FILE]], line: 14, type: ![[SUBTY:[0-9]+]]
// CIR-DAG: ![[SUBTY]] = !DISubroutineType{{.*}}types: ![[SUBTYS:[0-9]+]]
// CIR-DAG: ![[SUBTYS]] = !{![[INT:[0-9]+]], ![[PTR:[0-9]+]]}
// CIR-DAG: ![[PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[REC:[0-9]+]], size: 64)
// CIR-DAG: ![[REC]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "S", file: ![[#]], line: 9, size: 64, elements: ![[ELEMS:[0-9]+]])
// CIR-DAG: !DIFile(filename: "{{.*}}clang/test/CIR/CodeGen/debug-info-signature-record-shape.c", directory: "")
// CIR-DAG: ![[ELEMS]] = !{![[X:[0-9]+]], ![[Y:[0-9]+]]}
// CIR-DAG: ![[X]] = !DIDerivedType(tag: DW_TAG_member, name: "x", scope: ![[REC]], file: ![[#]], line: 10, baseType: ![[INT]], size: 32)
// CIR-DAG: ![[Y]] = !DIDerivedType(tag: DW_TAG_member, name: "y", scope: ![[REC]], file: ![[#]], line: 11, baseType: ![[INT]], size: 32, offset: 32)
// CIR-DAG: ![[ARG:[0-9]+]] = !DILocalVariable(name: "s", arg: 1, scope: ![[SP]], file: ![[FILE]], line: 14, type: ![[PTR]])

// OGCG-DAG: ![[SP:[0-9]+]] = distinct !DISubprogram(name: "f", scope: ![[FILE:[0-9]+]], file: ![[FILE]], line: 14, type: ![[SUBTY:[0-9]+]]
// OGCG-DAG: ![[SUBTY]] = !DISubroutineType{{.*}}types: ![[SUBTYS:[0-9]+]]
// OGCG-DAG: ![[SUBTYS]] = !{![[INT:[0-9]+]], ![[PTR:[0-9]+]]}
// OGCG-DAG: ![[PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[REC:[0-9]+]], size: 64)
// OGCG-DAG: ![[REC]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "S", file: ![[#]], line: 9, size: 64, elements: ![[ELEMS:[0-9]+]])
// OGCG-DAG: !DIFile(filename: "{{.*}}clang/test/CIR/CodeGen/debug-info-signature-record-shape.c", directory: "")
// OGCG-DAG: ![[ELEMS]] = !{![[X:[0-9]+]], ![[Y:[0-9]+]]}
// OGCG-DAG: ![[X]] = !DIDerivedType(tag: DW_TAG_member, name: "x", scope: ![[REC]], file: ![[#]], line: 10, baseType: ![[INT]], size: 32)
// OGCG-DAG: ![[Y]] = !DIDerivedType(tag: DW_TAG_member, name: "y", scope: ![[REC]], file: ![[#]], line: 11, baseType: ![[INT]], size: 32, offset: 32)
// OGCG-DAG: ![[ARG:[0-9]+]] = !DILocalVariable(name: "s", arg: 1, scope: ![[SP]], file: ![[FILE]], line: 14, type: ![[PTR]])
