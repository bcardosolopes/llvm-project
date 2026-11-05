// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -O1 %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t-O0.cir
// RUN: FileCheck --check-prefix=CIR-O0 --input-file=%t-O0.cir %s

// Ordinary local constant aggregate init uses a constant-global copy at both
// -O0 and -O1. The O1 split-store heuristic must not trigger on this path,
// matching OGCG's byte-typed guard in CGDecl.cpp.

void use(int *p);
void test(void) {
  int a[4] = {1, 2, 3, 4};
  use(a);
}

// CIR-DAG: @__const.test.a
// CIR-LABEL: cir.func{{.*}} @test()
// CIR: cir.get_global @__const.test.a
// CIR: cir.copy
// CIR-NOT: cir.libc.memset

// CIR-O0-DAG: @__const.test.a
// CIR-O0-LABEL: cir.func{{.*}} @test()
// CIR-O0: cir.get_global @__const.test.a
// CIR-O0: cir.copy
// CIR-O0-NOT: cir.libc.memset
