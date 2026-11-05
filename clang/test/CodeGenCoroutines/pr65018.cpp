// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:      -O1 -emit-llvm %s -o - | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:      -O1 -fexceptions -fcxx-exceptions -fclangir -emit-llvm \
// RUN:      %s -o - | FileCheck %s --check-prefix=CIR-O1 %}
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:      -O1 -fexceptions -fcxx-exceptions -fclangir -emit-llvm \
// RUN:      -disable-llvm-passes %s -o - \
// RUN:      | FileCheck %s --check-prefix=CIR-LLVM %}

#include "Inputs/coroutine.h"

// A simple awaiter type with an await_suspend method that can't be
// inlined.
struct Awaiter {
  const int& x;

  bool await_ready() { return false; }
  std::coroutine_handle<> await_suspend(const std::coroutine_handle<> h);
  void await_resume() {}
};

struct MyTask {
  // A lazy promise with an await_transform method that supports awaiting
  // integer references using the Awaiter struct above.
  struct promise_type {
    MyTask get_return_object() {
      return MyTask{
          std::coroutine_handle<promise_type>::from_promise(*this),
      };
    }

    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void unhandled_exception();

    auto await_transform(const int& x) { return Awaiter{x}; }
  };

  std::coroutine_handle<> h;
};

// A global array of integers.
int g_array[32];

// A coroutine that awaits each integer in the global array.
MyTask FooBar() {
  for (const int& x : g_array) {
    co_await x;
  }
}

// CHECK: %[[RET:.+]] = {{.*}}call{{.*}}@_ZN7Awaiter13await_suspendESt16coroutine_handleIvE
// CHECK: %[[RESUME_ADDR:.+]] = load ptr, ptr %[[RET]],
// CHECK: musttail call fastcc void %[[RESUME_ADDR]]({{.*}}%[[RET]]
// CHECK: ret

// CIR-O1-NOT: @__builtin_coro_await_suspend_handle
// CIR-O1-LABEL: define{{.*}}@_Z6FooBarv.resume
// CIR-O1: %[[RET:.+]] = {{.*}}@_ZN7Awaiter13await_suspendESt16coroutine_handleIvE
// CIR-O1: %[[RESUME_ADDR:.+]] = load ptr, ptr %[[RET]],
// CIR-O1: musttail call fastcc void %[[RESUME_ADDR]]({{.*}}%[[RET]]
// CIR-O1: ret

// CIR-LLVM-LABEL: define{{.*}}@_Z6FooBarv(
// CIR-LLVM: invoke void @llvm.coro.await.suspend.handle(ptr noundef %{{.+}}, ptr noundef %{{.+}}, ptr noundef @_Z6FooBarv.__await_suspend_wrapper__await)
// CIR-LLVM-NEXT: to label %[[AWAITCONT:[0-9]+]] unwind label %[[AWAITLPAD:[a-zA-Z0-9_.]+]]
// CIR-LLVM: [[AWAITLPAD]]:
// CIR-LLVM-NEXT: %{{.+}} = landingpad { ptr, i32 }
