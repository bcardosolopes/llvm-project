// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-jump-tables -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-jump-tables -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fno-jump-tables -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// CIR: "no-jump-tables" = "true"
// LLVM: "no-jump-tables"="true"
// OGCG: "no-jump-tables"="true"

int main(void) {
  return 0;
}
