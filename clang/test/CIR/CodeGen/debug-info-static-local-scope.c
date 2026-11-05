// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// A function-local static variable is scoped to its enclosing function's
// subprogram, not the compile unit, so a debugger shows it nested inside the
// function rather than as a file-scope global.

int counter(void) {
  static int n = 0;
  return ++n;
}

// CIR: !DIGlobalVariable(name: "n",{{.*}} scope: ![[SP:[0-9]+]]
// CIR: ![[SP]] = distinct !DISubprogram(name: "counter"

// OGCG: !DIGlobalVariable(name: "n",{{.*}} scope: ![[SP:[0-9]+]]
// OGCG: ![[SP]] = distinct !DISubprogram(name: "counter"
