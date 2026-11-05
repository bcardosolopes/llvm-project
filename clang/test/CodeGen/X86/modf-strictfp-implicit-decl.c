// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-unknown \
// RUN:   -Wno-implicit-function-declaration -w -emit-llvm \
// RUN:   -disable-llvm-passes -ffp-exception-behavior=strict -fclangir %s -o - | FileCheck %s %}
// RUN: %clang_cc1 -triple x86_64-unknown-unknown \
// RUN:   -Wno-implicit-function-declaration -w -emit-llvm \
// RUN:   -disable-llvm-passes -ffp-exception-behavior=strict %s -o - | FileCheck %s

#pragma STDC FENV_ACCESS ON

void foo(double x, double *ip) {
  modf(x, ip);
}

// CHECK-LABEL: define {{.*}} @foo(
// CHECK: call double @modf(double noundef %{{.*}}, ptr noundef %{{.*}}) #[[CALL_ATTR:[0-9]+]]
// CHECK: ret void
// CHECK: declare double @modf(double noundef, ptr noundef) #[[DECL_ATTR:[0-9]+]]
// CHECK: attributes #[[DECL_ATTR]] = { nounwind{{.*}} }
// CHECK: attributes #[[CALL_ATTR]] = { nounwind strictfp }
