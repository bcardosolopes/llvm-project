// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Verify that thread_local unions initialized via a non-first member correctly
// retype the global to match the active member's initializer (e.g.,
// union U -> struct { float }). Since `u` has a constexpr constructor and no
// destructor, no TLS wrapper is needed — the access goes directly through
// the thread-local address (matching OGCG behavior).

union U {
  int a;
  float f;
  constexpr U() : f(0.0) {}
};
static thread_local U u;
void *p = &u;

// The global is retyped to an anonymous struct matching the active member.
// CIR-DAG: cir.global "private"  internal tls_dyn dso_local  @_ZL1u = #cir.zero : !rec_anon_struct

// Direct access through thread-local address, with bitcast for retyped global.
// CIR-DAG: cir.get_global thread_local @_ZL1u : !cir.ptr<!rec_anon_struct>
// CIR-DAG: cir.cast bitcast {{.*}} !cir.ptr<!rec_anon_struct> -> !cir.ptr<!rec_U>

// No wrapper is generated for a constexpr-initialized internal TLS variable
// (matches OGCG — no _ZTWL1u wrapper in OGCG either).
// CIR-NOT: cir.func {{.*}} @_ZTWL1u

// LLVM: @_ZL1u = internal thread_local global { float }
// LLVM-NOT: @_ZTWL1u

// OGCG: @_ZL1u = internal thread_local global { float }
