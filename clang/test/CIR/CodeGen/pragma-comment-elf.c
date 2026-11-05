// RUN: %clang_cc1 %s -triple x86_64-unknown-linux-gnu -fms-extensions -fclangir -emit-llvm -o - | FileCheck %s
// RUN: %clang_cc1 %s -triple x86_64-unknown-linux-gnu -fms-extensions -emit-llvm -o - | FileCheck --check-prefix=OGCG %s

#pragma comment(lib, "msvcrt.lib")
#pragma comment(lib, "kernel32")
#pragma comment(lib, "USER32.LIB")
#pragma comment(lib, "with space")

void f(void) {}

// CHECK: !llvm.dependent-libraries = !{![[LIB0:[0-9]+]], ![[LIB1:[0-9]+]], ![[LIB2:[0-9]+]], ![[LIB3:[0-9]+]]}
// CHECK: ![[LIB0]] = !{!"msvcrt.lib"}
// CHECK: ![[LIB1]] = !{!"kernel32"}
// CHECK: ![[LIB2]] = !{!"USER32.LIB"}
// CHECK: ![[LIB3]] = !{!"with space"}

// OGCG: !llvm.dependent-libraries = !{![[LIB0:[0-9]+]], ![[LIB1:[0-9]+]], ![[LIB2:[0-9]+]], ![[LIB3:[0-9]+]]}
// OGCG: ![[LIB0]] = !{!"msvcrt.lib"}
// OGCG: ![[LIB1]] = !{!"kernel32"}
// OGCG: ![[LIB2]] = !{!"USER32.LIB"}
// OGCG: ![[LIB3]] = !{!"with space"}
