// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s
//
// Verify that internal-linkage thread_local variables that are never referenced
// through a TLS wrapper don't get spurious _ZTW/_ZTH wrapper/alias pairs.
// OGCG skips wrapper creation for discardable-linkage vars that aren't
// referenced at usage sites (ItaniumCXXABI.cpp:3245-3251). CIR must match.

int g();

// 'd' is file-scope static TLS with dynamic init but never used.
// No wrapper or init alias should be emitted.
static thread_local int d = g();

// 'anon_i' is anonymous-namespace TLS (also internal linkage) but IS used
// in set_anon_i(). It should get a wrapper and init alias.
namespace {
thread_local int anon_i{g()};
}
void set_anon_i() { anon_i = 2; }

// CHECK-NOT: @_ZTHL1d
// CHECK-NOT: @_ZTWL1d
// CHECK-DAG: @_ZTHN12_GLOBAL__N_16anon_iE = internal alias void (), ptr @__tls_init
// CHECK-DAG: define internal {{.*}} ptr @_ZTWN12_GLOBAL__N_16anon_iE()
