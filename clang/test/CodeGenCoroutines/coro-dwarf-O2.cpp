// Check that we can still observe the value of the coroutine frame
// with optimizations.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:   -emit-llvm %s -debug-info-kind=limited -dwarf-version=5 \
// RUN:   -O2 -o - | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:   -fexceptions -fcxx-exceptions -fclangir -disable-llvm-passes \
// RUN:   -emit-llvm %s -debug-info-kind=limited -dwarf-version=5 -O2 -o - \
// RUN:   | FileCheck %s --check-prefix=CIR-EH %}

#include "Inputs/coroutine.h"

template <>
struct std::coroutine_traits<void> {
  struct promise_type {
    void get_return_object();
    std::suspend_always initial_suspend();
    std::suspend_always final_suspend() noexcept;
    void return_void();
    void unhandled_exception();
  };
};

struct ScalarAwaiter {
  template <typename F> void await_suspend(F);
  bool await_ready();
  int await_resume();
};

extern "C" void UseScalar(int);

extern "C" void f() {
  UseScalar(co_await ScalarAwaiter{});

  int Val = co_await ScalarAwaiter{};

  co_await ScalarAwaiter{};
}

// CHECK: define {{.*}}@f.resume({{.*}} %[[ARG:.*]])
// CHECK:  #dbg_value(ptr %[[ARG]], ![[CORO_NUM:[0-9]+]], !DIExpression(DW_OP_deref)
// CHECK: ![[CORO_NUM]] = !DILocalVariable(name: "__coro_frame"
// CIR-EH: call void @llvm.coro.end(ptr null, i1 true, token none)
// CIR-EH: call i1 @llvm.coro.is_in_ramp()
// CIR-EH: call void @llvm.coro.end(ptr null, i1 false, token none)
