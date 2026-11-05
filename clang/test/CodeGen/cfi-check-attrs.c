// RUN: %clang_cc1 -triple arm-unknown-linux -funwind-tables=1 -fsanitize-cfi-cross-dso -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG
// RUN: %if cir-support %{ %clang_cc1 -triple arm-unknown-linux -funwind-tables=1 -fsanitize-cfi-cross-dso -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR %}

// OGCG-DAG: define weak arm_aapcscc void @__cfi_check({{.*}} [[OGATTR:#[0-9]*]]
// OGCG-DAG: define weak_odr hidden arm_aapcscc void @__cfi_check_fail(
// CIR-DAG: define weak arm_aapcscc void @__cfi_check({{.*}} [[CIRATTR:#[0-9]*]]
// CIR-DAG: define weak_odr hidden arm_aapcscc void @__cfi_check_fail(
// OGCG: attributes [[OGATTR]] = {{.*}} uwtable(sync)
// CIR: attributes [[CIRATTR]] = {{.*}} uwtable(sync)
