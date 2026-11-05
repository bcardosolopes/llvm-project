// RUN: %clang_cc1 -triple aarch64-unknown-linux -fsanitize-cfi-cross-dso -target-feature +reserve-x18 -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG
// RUN: %if cir-support %{ %clang_cc1 -triple aarch64-unknown-linux -fsanitize-cfi-cross-dso -target-feature +reserve-x18 -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR %}

// OGCG: @llvm.used = appending global [1 x ptr] [ptr @__cfi_check_fail]
// OGCG: define weak_odr hidden void @__cfi_check_fail{{.*}} [[ATTR:#[0-9]*]]
// OGCG: define weak void @__cfi_check{{.*}} [[ATTR]]

// OGCG: attributes [[ATTR]] = {{.*}} "target-features"="+reserve-x18"

// CIR: @llvm.used = appending global [1 x ptr] [ptr @__cfi_check_fail]
// CIR: define weak_odr hidden void @__cfi_check_fail{{.*}} [[ATTR:#[0-9]*]]
// CIR: define weak void @__cfi_check{{.*}} [[ATTR]]
// CIR: attributes [[ATTR]] = {{.*}} "target-features"="+reserve-x18"
