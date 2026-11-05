// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm -mstack-protector-guard=sysreg -mstack-protector-guard-reg=sp_el0 -mstack-protector-guard-offset=1024 %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -emit-llvm -mstack-protector-guard=sysreg -mstack-protector-guard-reg=sp_el0 -mstack-protector-guard-offset=1024 %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Stack-protector-guard module flags should be emitted.

void foo(int *a) {
  a[0] = 1;
}

// CHECK-DAG: !{i32 1, !"stack-protector-guard", !"sysreg"}
// CHECK-DAG: !{i32 1, !"stack-protector-guard-reg", !"sp_el0"}
// CHECK-DAG: !{i32 1, !"stack-protector-guard-offset", i32 1024}

// OGCG-DAG: !{i32 1, !"stack-protector-guard", !"sysreg"}
// OGCG-DAG: !{i32 1, !"stack-protector-guard-reg", !"sp_el0"}
// OGCG-DAG: !{i32 1, !"stack-protector-guard-offset", i32 1024}
