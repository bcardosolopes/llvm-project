// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fno-builtin-sin -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fno-builtin-sin -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

// Verify that -fno-builtin-<name> propagates "no-builtin-<name>" string
// attributes and "nobuiltin" enum attribute to call sites, matching OGCG.

extern "C" double sin(double);

double test_fno_builtin_sin(double x) {
  return sin(x);
}

// Both should have nobuiltin + "no-builtin-sin" on the call site.
// LLVM: call {{.*}} @sin({{.*}}) #[[NB:[0-9]+]]
// OGCG: call {{.*}} @sin({{.*}}) #[[NB:[0-9]+]]

// LLVM: attributes #[[NB]] = { nobuiltin "no-builtin-sin" }
// OGCG: attributes #[[NB]] = { nobuiltin "no-builtin-sin" }
