// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fvisibility=hidden -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fvisibility=hidden -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Verify that symbols with local (internal/private) linkage always get
// default visibility in the lowered LLVM IR, even when -fvisibility=hidden
// is specified. LLVM requires: local linkage → default visibility.

static int static_var = 42;
static int static_func() { return static_var; }

namespace {
  int anon_var = 10;
  int anon_func() { return anon_var; }
}

int caller() {
    return static_func() + anon_func();
}

// Internal-linkage globals: no "hidden" keyword.
// LLVM-DAG: @_ZL10static_var = internal global i32 42
// LLVM-DAG: @_ZN12_GLOBAL__N_18anon_varE = internal global i32 10

// Internal-linkage functions: "define internal" with no "hidden".
// LLVM-DAG: define internal {{.*}}@_ZL11static_funcv()
// LLVM-DAG: define internal {{.*}}@_ZN12_GLOBAL__N_19anon_funcEv()

// External caller should have hidden visibility.
// LLVM-DAG: define hidden {{.*}}@_Z6callerv()

// OGCG-DAG: @_ZL10static_var = internal global i32 42
// OGCG-DAG: @_ZN12_GLOBAL__N_18anon_varE = internal global i32 10
// OGCG-DAG: define internal {{.*}}@_ZL11static_funcv()
// OGCG-DAG: define internal {{.*}}@_ZN12_GLOBAL__N_19anon_funcEv()
// OGCG-DAG: define hidden {{.*}}@_Z6callerv()
