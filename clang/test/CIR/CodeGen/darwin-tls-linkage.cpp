// RUN: %clang_cc1 -std=c++20 -triple x86_64-apple-macos10.15 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// On Darwin, thread_local variables with external linkage should be
// downgraded to internal linkage because the thread-wrapper function
// handles all external access. Exception: constinit variables can be
// referenced directly.

thread_local int tls_var = 42;
constinit thread_local int tls_constinit = 99;

// Regular thread_local: downgraded to internal on Darwin.
// CHECK: cir.global
// CHECK-SAME: internal tls_dyn
// CHECK-SAME: @tls_var

// constinit thread_local: keeps external linkage.
// CHECK: cir.global
// CHECK-SAME: external tls_dyn
// CHECK-SAME: @tls_constinit
