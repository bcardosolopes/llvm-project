// RUN: %clang_cc1 -triple x86_64-apple-macosx10.8 -std=c++1y -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=DARWIN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++1y -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=LINUX

// Test that Darwin TLS init functions and wrappers use cxx_fast_tlscc,
// matching OGCG behavior.

struct A {
  ~A();
};

thread_local A a;

// DARWIN: define cxx_fast_tlscc noundef ptr @_ZTW1a()
// DARWIN: call cxx_fast_tlscc void @_ZTH1a()
// DARWIN: define internal cxx_fast_tlscc void @__tls_init()

// LINUX-NOT: cxx_fast_tlscc
