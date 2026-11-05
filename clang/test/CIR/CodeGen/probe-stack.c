// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fstack-clash-protection -mstack-probe-size=8192 %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fstack-clash-protection -mstack-probe-size=8192 %s -o %t-cir.mlir
// RUN: FileCheck --check-prefix=CIR --input-file=%t-cir.mlir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fstack-clash-protection -mstack-probe-size=8192 %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// probe-stack and stack-probe-size module flags should be emitted.

void foo(int *a) { a[0] = 1; }

// CHECK-DAG: !{i32 4, !"probe-stack", !"inline-asm"}
// CHECK-DAG: !{i32 8, !"stack-probe-size", i32 8192}
// CIR: "probe-stack" = "inline-asm"
// CIR: "stack-probe-size" = "8192"

// OGCG-DAG: !{i32 4, !"probe-stack", !"inline-asm"}
// OGCG-DAG: !{i32 8, !"stack-probe-size", i32 8192}
