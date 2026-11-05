// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN:   -emit-llvm -debug-info-kind=limited %s -o %t.mem2reg.ll
// RUN: FileCheck --input-file=%t.mem2reg.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

typedef int I;
typedef long L;

// Shadowed same-name locals with distinct typedef sugar must keep the source
// type for each lexical scope after DirectToLLVM debug-type repair.
void shadowed_locals(void) {
  I x = 0;
  {
    L x = 1;
    (void)x;
  }
  (void)x;
}

// CIR-DAG: ![[FUNC:[0-9]+]] = distinct !DISubprogram(name: "shadowed_locals"
// CIR-DAG: ![[INNER_SCOPE:[0-9]+]] = distinct !DILexicalBlock(scope: ![[FUNC]], {{.*}}line: 18, column: 3)
// CIR-DAG: ![[INT:[0-9]+]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CIR-DAG: ![[LONG:[0-9]+]] = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
// CIR-DAG: ![[TYPE_I:[0-9]+]] = !DIDerivedType(tag: DW_TAG_typedef, name: "I", {{.*}}baseType: ![[INT]])
// CIR-DAG: ![[TYPE_L:[0-9]+]] = !DIDerivedType(tag: DW_TAG_typedef, name: "L", {{.*}}baseType: ![[LONG]])
// CIR-DAG: ![[OUTER:[0-9]+]] = !DILocalVariable(name: "x", scope: ![[FUNC]], {{.*}}type: ![[TYPE_I]])
// CIR-DAG: ![[INNER:[0-9]+]] = !DILocalVariable(name: "x", scope: ![[INNER_SCOPE]], {{.*}}type: ![[TYPE_L]])
// CIR-DAG: #dbg_{{declare|value}}({{.*}}, ![[OUTER]], !DIExpression(),
// CIR-DAG: #dbg_{{declare|value}}({{.*}}, ![[INNER]], !DIExpression(),

// OGCG-DAG: ![[FUNC:[0-9]+]] = distinct !DISubprogram(name: "shadowed_locals"
// OGCG-DAG: ![[INNER_SCOPE:[0-9]+]] = distinct !DILexicalBlock(scope: ![[FUNC]], {{.*}}line: 18, column: 3)
// OGCG-DAG: ![[INT:[0-9]+]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// OGCG-DAG: ![[LONG:[0-9]+]] = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
// OGCG-DAG: ![[TYPE_I:[0-9]+]] = !DIDerivedType(tag: DW_TAG_typedef, name: "I", {{.*}}baseType: ![[INT]])
// OGCG-DAG: ![[TYPE_L:[0-9]+]] = !DIDerivedType(tag: DW_TAG_typedef, name: "L", {{.*}}baseType: ![[LONG]])
// OGCG-DAG: ![[OUTER:[0-9]+]] = !DILocalVariable(name: "x", scope: ![[FUNC]], {{.*}}type: ![[TYPE_I]])
// OGCG-DAG: ![[INNER:[0-9]+]] = !DILocalVariable(name: "x", scope: ![[INNER_SCOPE]], {{.*}}type: ![[TYPE_L]])
