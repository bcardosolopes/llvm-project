// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Verify that co_await / co_yield expressions returning aggregate types work.
// Previously crashed with "AggExprEmitter: VisitCoawaitExpr" NYI.

namespace std {
template <class Promise = void>
struct coroutine_handle {
  static coroutine_handle from_address(void *) noexcept;
};
template <>
struct coroutine_handle<void> {
  template <class PromiseType>
  coroutine_handle(coroutine_handle<PromiseType>) noexcept;
  static coroutine_handle from_address(void *);
};
template <class Ret, typename... T>
struct coroutine_traits { using promise_type = typename Ret::promise_type; };
struct suspend_never {
  bool await_ready() noexcept { return true; }
  void await_suspend(coroutine_handle<>) noexcept {}
  void await_resume() noexcept {}
};
} // namespace std

struct S { int x; int y; };

struct Awaiter {
  S await_resume() { return {42, 100}; }
  bool await_ready() { return true; }
  void await_suspend(std::coroutine_handle<>) {}
};

struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

Task test_coawait_aggregate() {
  S s = co_await Awaiter{};
  (void)s;
}

// CIR: cir.func coroutine {{.*}} @_Z22test_coawait_aggregatev()
// CIR: cir.await(user,
// CIR: resume : {
// CIR:   cir.call @_ZN7Awaiter12await_resumeEv(

// LLVM: define {{.*}} @_Z22test_coawait_aggregatev
