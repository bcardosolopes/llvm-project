// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=line-tables-only %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=line-tables-only %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// In line-tables-only mode, variable debug metadata/declares should be absent.
// OGCG also keeps the DISubroutineType type array empty in DWARF mode.

int f(int x) {
  int y = x + 1;
  return y;
}

// CIR: !DICompileUnit({{.*}}emissionKind: LineTablesOnly
// CIR-DAG: ![[CIR_EMPTY:[0-9]+]] = !{}
// CIR-DAG: ![[CIR_FTY:[0-9]+]] = !DISubroutineType(types: ![[CIR_EMPTY]])
// CIR-DAG: ![[CIR_F:[0-9]+]] = distinct !DISubprogram(name: "f"{{.*}}type: ![[CIR_FTY]]
// CIR-NOT: #dbg_declare
// CIR-NOT: !DILocalVariable

// OGCG: !DICompileUnit({{.*}}emissionKind: LineTablesOnly
// OGCG-DAG: ![[OGCG_EMPTY:[0-9]+]] = !{}
// OGCG-DAG: ![[OGCG_FTY:[0-9]+]] = !DISubroutineType(types: ![[OGCG_EMPTY]])
// OGCG-DAG: ![[OGCG_F:[0-9]+]] = distinct !DISubprogram(name: "f"{{.*}}type: ![[OGCG_FTY]]
// OGCG-NOT: #dbg_declare
// OGCG-NOT: !DILocalVariable
