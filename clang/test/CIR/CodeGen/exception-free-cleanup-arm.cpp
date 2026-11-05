// RUN: %clang_cc1 -std=c++17 -triple arm-none-eabi -fcxx-exceptions -fexceptions -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple arm-none-eabi -fcxx-exceptions -fexceptions -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM

struct S {
  S();
  ~S();
};

void f() { throw S(); }

// CIR: cir.func private @__cxa_free_exception(!cir.ptr<!void>)
// CIR-LABEL: cir.func no_inline optnone dso_local @_Z1fv()
// CIR: cir.call @__cxa_free_exception(%{{.*}}) nothrow : (!cir.ptr<!void>) -> ()

// LLVM: declare void @__cxa_free_exception(ptr)
// LLVM-LABEL: define dso_local void @_Z1fv()
// LLVM: call void @__cxa_free_exception(ptr %{{.*}})
