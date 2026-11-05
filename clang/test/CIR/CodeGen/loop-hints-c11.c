// RUN: %clang_cc1 -std=c11 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR

void f(int n) {
  int i = 0;
  while (i < n)
    ++i;
}

// CIR: cir.func{{.*}} @f(
// CIR:   cir.while {
// CIR:   } do {
// CIR:   } {loopHint = {mustprogress = true}}
