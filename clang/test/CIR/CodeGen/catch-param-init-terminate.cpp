// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Per C++ [except.handle]/7, if the copy/move constructor of a catch
// variable throws during initialization, std::terminate() must be called.

struct T { T(); ~T(); };
struct A {
  A(const A &x, const T &t = T());
  ~A();
};
void foo();

void catch_param_init_calls_terminate() {
  try {
    foo();
  } catch (A a) {
  }
}

// CIR-LABEL: cir.func{{.*}} @_Z32catch_param_init_calls_terminatev
// CIR: cir.try synthetic {
// CIR:   cir.call exception @_ZN1TC1Ev
// CIR:   cir.yield
// CIR: } catch all {
// CIR:   cir.catch_param
// CIR:   cir.call @_ZSt9terminatev
// CIR:   cir.unreachable
// CIR: }

// Both T's ctor and A's copy ctor should invoke to a shared terminate landing pad.
// LLVM-LABEL: define {{.*}} @_Z32catch_param_init_calls_terminatev
// LLVM: call ptr @__cxa_get_exception_ptr
// LLVM: invoke void @_ZN1TC1Ev
// LLVM: invoke void @_ZN1AC1ERKS_RK1T
// Both invokes share a single deduplicated terminate landing pad.
// LLVM: landingpad { ptr, i32 }
// LLVM-NEXT: catch ptr null
// LLVM: call void @__clang_call_terminate

// OGCG-LABEL: define {{.*}} @_Z32catch_param_init_calls_terminatev
// OGCG: call ptr @__cxa_get_exception_ptr
// OGCG: invoke void @_ZN1TC1Ev
// OGCG: invoke void @_ZN1AC1ERKS_RK1T
// OGCG: landingpad { ptr, i32 }
// OGCG-NEXT: catch ptr null
// OGCG: call void @__clang_call_terminate
