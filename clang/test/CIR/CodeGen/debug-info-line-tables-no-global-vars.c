// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=line-tables-only %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=line-tables-only %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// In line-tables-only mode, OGCG does not emit global variable debug metadata.
// CIR should match and avoid attaching DIGlobalVariableExpression to globals.
// Function signatures should likewise keep an empty DISubroutineType array.

int glob = 17;

int f(int x) {
  int y = x + glob;
  return y;
}

// CIR: @glob = global i32 17, align 4
// CIR-DAG: ![[CIR_EMPTY:[0-9]+]] = !{}
// CIR-DAG: ![[CIR_FTY:[0-9]+]] = !DISubroutineType(types: ![[CIR_EMPTY]])
// CIR-DAG: ![[CIR_F:[0-9]+]] = distinct !DISubprogram(name: "f"{{.*}}type: ![[CIR_FTY]]
// CIR-NOT: !DIGlobalVariable(name: "glob"
// CIR-NOT: #dbg_declare
// CIR-NOT: !DILocalVariable

// OGCG: @glob = global i32 17, align 4
// OGCG-DAG: ![[OGCG_EMPTY:[0-9]+]] = !{}
// OGCG-DAG: ![[OGCG_FTY:[0-9]+]] = !DISubroutineType(types: ![[OGCG_EMPTY]])
// OGCG-DAG: ![[OGCG_F:[0-9]+]] = distinct !DISubprogram(name: "f"{{.*}}type: ![[OGCG_FTY]]
// OGCG-NOT: !DIGlobalVariable(name: "glob"
// OGCG-NOT: #dbg_declare
// OGCG-NOT: !DILocalVariable
