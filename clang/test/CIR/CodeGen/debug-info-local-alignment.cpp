// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited -std=c++17 %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited -std=c++17 %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A local variable with an explicit over-alignment must record DW_AT_alignment
// (the DILocalVariable "align" field, in bits); a plainly-aligned local records
// none.

double sink;
void f() {
  alignas(32) int la = 1;
  __attribute__((aligned(16))) double ld = 2;
  int normal = 3;
  sink = la + ld + normal;
}

// CIR-DAG: !DILocalVariable(name: "la",{{.*}} align: 256)
// CIR-DAG: !DILocalVariable(name: "ld",{{.*}} align: 128)
// CIR-DAG: !DILocalVariable(name: "normal", scope: {{![0-9]+}}, file: {{![0-9]+}}, line: {{[0-9]+}}, type: {{![0-9]+}})

// OGCG-DAG: !DILocalVariable(name: "la",{{.*}} align: 256)
// OGCG-DAG: !DILocalVariable(name: "ld",{{.*}} align: 128)
// OGCG-DAG: !DILocalVariable(name: "normal", scope: {{![0-9]+}}, file: {{![0-9]+}}, line: {{[0-9]+}}, type: {{![0-9]+}})
