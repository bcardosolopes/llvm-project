// Verify that CIR correctly sets dso_local on internal-linkage cpu_dispatch
// ifuncs and their aliases. Internal linkage globals must be dso_local per
// the LLVM verifier.
//
// RUN: %clang_cc1 -std=c++11 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// RUN: %clang_cc1 -std=c++11 -triple x86_64-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

__attribute__((cpu_dispatch(atom, generic)))
static int static_dispatch() {}

namespace {
__attribute__((cpu_dispatch(atom, generic)))
int anon_dispatch() {}
}

int usage() {
  return static_dispatch() + anon_dispatch();
}

// Both the alias and ifunc must have internal linkage (and be dso_local,
// which LLVM implicitly verifies for internal globals).

// CHECK: @_ZL15static_dispatchv = internal alias i32 (), ptr @_ZL15static_dispatchv.ifunc
// CHECK: @_ZN12_GLOBAL__N_113anon_dispatchEv = internal alias i32 (), ptr @_ZN12_GLOBAL__N_113anon_dispatchEv.ifunc
// CHECK: @_ZL15static_dispatchv.ifunc = internal ifunc i32 (), ptr @_ZL15static_dispatchv.resolver
// CHECK: @_ZN12_GLOBAL__N_113anon_dispatchEv.ifunc = internal ifunc i32 (), ptr @_ZN12_GLOBAL__N_113anon_dispatchEv.resolver

// OGCG: @_ZL15static_dispatchv = internal alias i32 (), ptr @_ZL15static_dispatchv.ifunc
// OGCG: @_ZN12_GLOBAL__N_113anon_dispatchEv = internal alias i32 (), ptr @_ZN12_GLOBAL__N_113anon_dispatchEv.ifunc
// OGCG: @_ZL15static_dispatchv.ifunc = internal ifunc i32 (), ptr @_ZL15static_dispatchv.resolver
// OGCG: @_ZN12_GLOBAL__N_113anon_dispatchEv.ifunc = internal ifunc i32 (), ptr @_ZN12_GLOBAL__N_113anon_dispatchEv.resolver
