// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -ffp-exception-behavior=strict %s -o - | FileCheck %s --check-prefix=OGCG-STRICT
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir %s -o - | FileCheck %s --check-prefix=CIR %}
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -ffp-exception-behavior=strict -fclangir %s -o - | FileCheck %s --check-prefix=CIR-STRICT %}

// CIR-STRICT-NOT: @__builtin_modf

// OGCG-LABEL: @test_modf(
// OGCG: call { double, double } @llvm.modf.f64(double
//
// OGCG-STRICT-LABEL: @test_modf(
// OGCG-STRICT: call double @modf(double noundef
//
// CIR-LABEL: @test_modf(
// CIR: call { double, double } @llvm.modf.f64(double
//
// CIR-STRICT-LABEL: @test_modf(
// CIR-STRICT: call double @modf(double
//
double test_modf(double x, double *ip) {
  return __builtin_modf(x, ip);
}
