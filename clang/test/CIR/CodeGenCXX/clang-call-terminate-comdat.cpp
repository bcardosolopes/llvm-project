// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++2a -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++2a -fexceptions -fcxx-exceptions -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Use a typed catch with a non-trivial destructor so that OGCG wraps
// __cxa_end_catch in an invoke with a __clang_call_terminate landing pad.
// A simple catch(...) does not trigger __clang_call_terminate in OGCG.

struct S { S(const S&); ~S(); };
void may_throw();
void g(S);
void test() {
  try { may_throw(); } catch (S s) { g(s); }
}

// CHECK: $__clang_call_terminate = comdat any
// CHECK: define linkonce_odr hidden void @__clang_call_terminate({{.*}}) {{.*}} comdat

// OGCG: $__clang_call_terminate = comdat any
// OGCG: define linkonce_odr hidden void @__clang_call_terminate({{.*}}) {{.*}} comdat
