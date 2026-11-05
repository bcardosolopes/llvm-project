// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffast-math -ffp-contract=fast -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=FAST
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffast-math -ffp-contract=fast -emit-llvm -o - %s | FileCheck %s --check-prefix=FAST
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffast-math -ffp-contract=fast -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=CIR-FAST

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -menable-no-infs -menable-no-nans -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=FINITE
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -menable-no-infs -menable-no-nans -emit-llvm -o - %s | FileCheck %s --check-prefix=FINITE
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -menable-no-infs -menable-no-nans -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=CIR-FINITE

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=NONE
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=CIR-NONE

float f0, f1, f2;

void test_fadd(void) {
    f0 = f1 + f2;
}
// FAST-LABEL: define {{.*}} @test_fadd
// FAST: fadd fast float
// CIR-FAST-LABEL: @test_fadd
// CIR-FAST: cir.binop(add, {{%.*}}, {{%.*}}) fast : !cir.float

// FINITE-LABEL: define {{.*}} @test_fadd
// FINITE: fadd nnan ninf float
// CIR-FINITE-LABEL: @test_fadd
// CIR-FINITE: cir.binop(add, {{%.*}}, {{%.*}}) nnan|ninf : !cir.float

// No fast-math flags without -ffast-math
// NONE-LABEL: define {{.*}} @test_fadd
// NONE: fadd float
// CIR-NONE-LABEL: @test_fadd
// CIR-NONE: cir.binop(add, {{%.*}}, {{%.*}}) : !cir.float

void test_fsub(void) {
    f0 = f1 - f2;
}
// FAST-LABEL: define {{.*}} @test_fsub
// FAST: fsub fast float
// CIR-FAST-LABEL: @test_fsub
// CIR-FAST: cir.binop(sub, {{%.*}}, {{%.*}}) fast : !cir.float

void test_fmul(void) {
    f0 = f1 * f2;
}
// FAST-LABEL: define {{.*}} @test_fmul
// FAST: fmul fast float
// CIR-FAST-LABEL: @test_fmul
// CIR-FAST: cir.binop(mul, {{%.*}}, {{%.*}}) fast : !cir.float

void test_fdiv(void) {
    f0 = f1 / f2;
}
// FAST-LABEL: define {{.*}} @test_fdiv
// FAST: fdiv fast float
// CIR-FAST-LABEL: @test_fdiv
// CIR-FAST: cir.binop(div, {{%.*}}, {{%.*}}) fast : !cir.float

void test_compound_assign(void) {
    f0 += f1;
}
// FAST-LABEL: define {{.*}} @test_compound_assign
// FAST: fadd fast float
// CIR-FAST-LABEL: @test_compound_assign
// CIR-FAST: cir.binop(add, {{%.*}}, {{%.*}}) fast : !cir.float
