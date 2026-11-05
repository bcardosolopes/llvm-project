// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN:   -emit-llvm -debug-info-kind=limited %s -o %t.mem2reg.ll
// RUN: FileCheck --input-file=%t.mem2reg.ll %s --check-prefix=CIR
// RUN: %clang -target x86_64-unknown-linux-gnu -g -S -emit-llvm -fclangir %s \
// RUN:   -o %t.driver.ll
// RUN: FileCheck --input-file=%t.driver.ll %s --check-prefix=DRIVER
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

int sum3(int a, int b, int c) { return a + b + c; }

// CIR-DAG: ![[A:[0-9]+]] = !DILocalVariable(name: "a", arg: 1,
// CIR-DAG: ![[B:[0-9]+]] = !DILocalVariable(name: "b", arg: 2,
// CIR-DAG: ![[C:[0-9]+]] = !DILocalVariable(name: "c", arg: 3,

// DRIVER-DAG: ![[A:[0-9]+]] = !DILocalVariable(name: "a", arg: 1,
// DRIVER-DAG: ![[B:[0-9]+]] = !DILocalVariable(name: "b", arg: 2,
// DRIVER-DAG: ![[C:[0-9]+]] = !DILocalVariable(name: "c", arg: 3,

// OGCG-DAG: ![[A:[0-9]+]] = !DILocalVariable(name: "a", arg: 1,
// OGCG-DAG: ![[B:[0-9]+]] = !DILocalVariable(name: "b", arg: 2,
// OGCG-DAG: ![[C:[0-9]+]] = !DILocalVariable(name: "c", arg: 3,
