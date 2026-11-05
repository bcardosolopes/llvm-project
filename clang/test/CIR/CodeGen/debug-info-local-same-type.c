// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Compile-time regression guard:
// repeated same-typed locals should keep correct debug info while allowing
// DirectToLLVM to cache type mapping work.
int same_type_locals(int a) {
  int x0 = a + 0;
  int x1 = a + 1;
  int x2 = a + 2;
  int x3 = a + 3;
  int x4 = a + 4;
  int x5 = a + 5;
  int x6 = a + 6;
  int x7 = a + 7;
  return x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7;
}

// CIR-DAG: ![[A:[0-9]+]] = !DILocalVariable(name: "a", arg: 1,
// CIR-DAG: ![[X0:[0-9]+]] = !DILocalVariable(name: "x0", scope:
// CIR-DAG: ![[X7:[0-9]+]] = !DILocalVariable(name: "x7", scope:
// CIR-DAG: #dbg_declare(ptr %{{[0-9]+}}, ![[X0]], !DIExpression()
// CIR-DAG: #dbg_declare(ptr %{{[0-9]+}}, ![[X7]], !DIExpression()

// OGCG-DAG: ![[A:[0-9]+]] = !DILocalVariable(name: "a", arg: 1,
// OGCG-DAG: ![[X0:[0-9]+]] = !DILocalVariable(name: "x0",
// OGCG-DAG: ![[X7:[0-9]+]] = !DILocalVariable(name: "x7",
// OGCG-DAG: #dbg_declare(ptr %{{.*}}, ![[X0]], !DIExpression()
// OGCG-DAG: #dbg_declare(ptr %{{.*}}, ![[X7]], !DIExpression()
