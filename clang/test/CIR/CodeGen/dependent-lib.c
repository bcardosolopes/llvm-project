// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm --dependent-lib=msvcrt --dependent-lib=kernel32 %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm --dependent-lib=msvcrt --dependent-lib=kernel32 %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// !llvm.dependent-libraries should be emitted for ELF targets.

void f(void) {}

// CHECK: !llvm.dependent-libraries = !{![[LIB0:[0-9]+]], ![[LIB1:[0-9]+]]}
// CHECK-DAG: ![[LIB0]] = !{!"msvcrt"}
// CHECK-DAG: ![[LIB1]] = !{!"kernel32"}

// OGCG: !llvm.dependent-libraries = !{![[LIB0:[0-9]+]], ![[LIB1:[0-9]+]]}
// OGCG-DAG: ![[LIB0]] = !{!"msvcrt"}
// OGCG-DAG: ![[LIB1]] = !{!"kernel32"}
