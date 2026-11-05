// RUN: %clang_cc1 -triple arm64-apple-darwin9 -target-feature +neon \
// RUN:   -ffreestanding -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple arm64-apple-darwin9 -target-feature +neon \
// RUN:   -ffreestanding -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR

extern void complex_callee(__complex__ double);

// OGCG-LABEL: define{{.*}} void @test_complex_long([2 x double] noundef %{{.*}})
// OGCG: call void @complex_callee([2 x double] noundef %{{.*}})
// CIR-LABEL: define{{.*}} void @test_complex_long([2 x double] noundef %{{.*}})
// CIR: call void @complex_callee([2 x double] noundef %{{.*}})
void test_complex_long(__complex__ long double cd) { complex_callee(cd); }
