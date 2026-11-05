// RUN: %clang_cc1 -triple x86_64-apple-darwin12 -std=c++20 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-apple-darwin12 -std=c++20 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-apple-darwin12 -std=c++20 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=OGCG

// On Darwin, TLS wrapper and init functions should use cxx_fast_tlscc
// calling convention for optimal TLS access fastpath.

// --- Case 1: Defined thread_local variable ---
thread_local int tls_var = 42;

int get_tls() {
  return tls_var;
}

// Wrapper function should have cxx_fast_tlscc.
// CIR: cir.func {{.*}} @_ZTW7tls_var() -> {{.*}} cc(cxx_fast_tls)

// --- Case 2: Extern thread_local variable ---
extern thread_local int ext_tls;

int get_ext() {
  return ext_tls;
}

// Extern wrapper declaration and call should both have cxx_fast_tlscc.
// LLVM: declare cxx_fast_tlscc {{.*}}ptr @_ZTW7ext_tls()
// LLVM-LABEL: define noundef i32 @_Z7get_extv()
// LLVM: call cxx_fast_tlscc {{.*}}ptr @_ZTW7ext_tls()
// LLVM-LABEL: define cxx_fast_tlscc noundef ptr @_ZTW7tls_var()
// LLVM-NOT: define weak_odr hidden cxx_fast_tlscc{{.*}} @_ZTW7tls_var()
// OGCG-LABEL: define noundef i32 @_Z7get_extv()
// OGCG: call cxx_fast_tlscc {{.*}}ptr @_ZTW7ext_tls()
// OGCG: declare cxx_fast_tlscc {{.*}}ptr @_ZTW7ext_tls()
// OGCG-LABEL: define cxx_fast_tlscc noundef ptr @_ZTW7tls_var()
