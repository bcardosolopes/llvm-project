// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

// Verify that __attribute__((no_builtin("name"))) propagates "no-builtin-name"
// string attribute to the function definition, matching OGCG.

extern "C" double sin(double);
extern "C" double cos(double);

// LLVM-LABEL: define {{.*}} @_Z24test_per_func_no_builtind
// OGCG-LABEL: define {{.*}} @_Z24test_per_func_no_builtind
__attribute__((no_builtin("sin")))
double test_per_func_no_builtin(double x) {
  // sin should be a regular call (not lowered to intrinsic).
  // LLVM: call {{.*}}double @sin(
  // OGCG: call {{.*}}double @sin(
  //
  // cos should still be lowered to the intrinsic since it's not disabled.
  // LLVM: call double @llvm.cos.f64(
  // OGCG: call double @llvm.cos.f64(
  return sin(x) + cos(x);
}

// The function should have "no-builtin-sin" attribute.
// LLVM: attributes #{{[0-9]+}} = {{{.*}}"no-builtin-sin"
// OGCG: attributes #{{[0-9]+}} = {{{.*}}"no-builtin-sin"
