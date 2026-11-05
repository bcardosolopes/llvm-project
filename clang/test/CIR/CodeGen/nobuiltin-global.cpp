// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fno-builtin -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fno-builtin -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

// Verify that -fno-builtin (global) propagates "no-builtins" string attribute
// and "nobuiltin" enum attribute to call sites, matching OGCG.

extern "C" double sin(double);
extern "C" double cos(double);

double test_fno_builtin_global(double x) {
  return sin(x) + cos(x);
}

// Both sin and cos should have nobuiltin + "no-builtins" on the call site.
// LLVM: call {{.*}} @sin({{.*}}) #[[NB:[0-9]+]]
// LLVM: call {{.*}} @cos({{.*}}) #[[NB]]
// OGCG: call {{.*}} @sin({{.*}}) #[[NB:[0-9]+]]
// OGCG: call {{.*}} @cos({{.*}}) #[[NB]]

// LLVM: attributes #[[NB]] = { nobuiltin "no-builtins" }
// OGCG: attributes #[[NB]] = { nobuiltin "no-builtins" }
