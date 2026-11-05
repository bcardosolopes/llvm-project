// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited -O2 %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited -O2 %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

struct S {
  int a;
};

int f(struct S *p) {
  struct S *q = p;
  return q->a;
}

// CIR-DAG: ![[STRUCT:[0-9]+]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "S"
// CIR-DAG: ![[PTR:[0-9]+]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[STRUCT]], size: 64)
// CIR-DAG: ![[SP:[0-9]+]] = distinct !DISubprogram(name: "f", scope: ![[#]], file: ![[#]], line: [[#]], type: ![[#]], scopeLine: [[#]], flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: ![[#]], retainedNodes: ![[RET:[0-9]+]])
// CIR-DAG: ![[PVAR:[0-9]+]] = !DILocalVariable(name: "p", arg: 1, scope: ![[SP]], file: ![[#]], line: [[#]], type: ![[PTR]])
// CIR-DAG: ![[QVAR:[0-9]+]] = !DILocalVariable(name: "q", scope: ![[SP]], file: ![[#]], line: [[#]], type: ![[PTR]])
// CIR-DAG: ![[RET]] = !{![[PVAR]], ![[QVAR]]}
// CIR-NOT: !DILexicalBlock(scope: ![[SP]], file: ![[#]], line: 12, column: 7)
// CIR-NOT: !DILexicalBlock(scope: ![[SP]], file: ![[#]], line: 13, column: 3)
// CIR-NOT: !DIBasicType(name: "void", encoding: DW_ATE_address)

// OGCG-DAG: ![[SP:[0-9]+]] = distinct !DISubprogram(name: "f", scope: ![[#]], file: ![[#]], line: [[#]], type: ![[#]], scopeLine: [[#]], flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: ![[#]], retainedNodes: ![[RET:[0-9]+]])
// OGCG-DAG: ![[STRUCT:[0-9]+]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "S"
// OGCG-DAG: ![[PTR:[0-9]+]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[STRUCT]], size: 64)
// OGCG-DAG: ![[PVAR:[0-9]+]] = !DILocalVariable(name: "p", arg: 1, scope: ![[#]], file: ![[#]], line: [[#]], type: ![[PTR]])
// OGCG-DAG: ![[QVAR:[0-9]+]] = !DILocalVariable(name: "q", scope: ![[#]], file: ![[#]], line: [[#]], type: ![[PTR]])
// OGCG-DAG: ![[RET]] = !{![[PVAR]], ![[QVAR]]}
