// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -mskip-rax-setup \
// RUN:   -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=SKIP %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-llvm %s -o %t.cir.noskip.ll
// RUN: FileCheck --input-file=%t.cir.noskip.ll --check-prefix=NO-SKIP %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mskip-rax-setup \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

void f(void) {}

// SKIP: !"SkipRaxSetup", i32 1}
// NO-SKIP-NOT: "SkipRaxSetup"
// OGCG: !"SkipRaxSetup", i32 1}
