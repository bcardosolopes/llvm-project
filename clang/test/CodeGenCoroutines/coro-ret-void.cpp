// RUN: %clang_cc1 -std=c++20 -triple=x86_64-unknown-linux-gnu -emit-llvm %s -o - -disable-llvm-passes | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -std=c++20 -triple=x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fexceptions -fcxx-exceptions -emit-llvm %s -o - -disable-llvm-passes \
// RUN:   | FileCheck %s --check-prefix=CIR-LLVM %}

#include "Inputs/coroutine.h"

struct coro1 {
  struct promise_type {
    coro1 get_return_object();
    std::suspend_never initial_suspend();
    std::suspend_never final_suspend() noexcept;
    void return_void();
    void unhandled_exception() noexcept;
  };
};

coro1 f() {
  co_await std::suspend_never{};
}

// CHECK-LABEL: define{{.*}} void @_Z1fv(
// CHECK: call void @_ZNSt13suspend_never12await_resumeEv(ptr
// CHECK: call void @_ZN5coro112promise_type11return_voidEv(ptr {{[^,]*}} %__promise)
// CIR-LLVM-LABEL: define dso_local void @_Z1fv() {{.*}} personality ptr @__gxx_personality_v0 {
// CIR-LLVM: invoke void @_ZN5coro112promise_type11return_voidEv(ptr noundef nonnull align 1 dereferenceable(1) %1)
// CIR-LLVM-NEXT: to label %{{.+}} unwind label %[[RETVOIDLPAD:[a-zA-Z0-9_.]+]]
// CIR-LLVM: [[RETVOIDLPAD]]:
// CIR-LLVM-NEXT: %{{.+}} = landingpad { ptr, i32 }
// CIR-LLVM: call void @_ZN5coro112promise_type19unhandled_exceptionEv(ptr noundef nonnull align 1 dereferenceable(1) %1)

struct A {
  A();
  ~A();
};

coro1 f2() {
  co_return (void) A{};
}

// CHECK-LABEL: define{{.*}} void @_Z2f2v(
// CHECK: call void @_ZN1AC1Ev(ptr {{[^,]*}} %[[AVar:.*]])
// CHECK-NEXT: call void @_ZN1AD1Ev(ptr {{[^,]*}} %[[AVar]])
// CHECK-NEXT: call void @_ZN5coro112promise_type11return_voidEv(ptr

struct coro2 {
  struct promise_type {
    coro2 get_return_object();
    std::suspend_never initial_suspend();
    std::suspend_never final_suspend() noexcept;
    void return_value(int);
    void unhandled_exception() noexcept;
  };
};

coro2 g() {
  co_return 42;
}

// CHECK-LABEL: define{{.*}} void @_Z1gv(
// CHECK: call void @_ZNSt13suspend_never12await_resumeEv(ptr
// CHECK: call void @_ZN5coro212promise_type12return_valueEi(ptr {{[^,]*}} %__promise, i32 noundef 42)
