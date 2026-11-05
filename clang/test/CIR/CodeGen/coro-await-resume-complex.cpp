// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm -std=c++20 \
// RUN:   -fcxx-exceptions -fexceptions -o /dev/null %s 2>&1 \
// RUN:   | FileCheck --allow-empty %s
// CHECK-NOT: NYI
// CHECK-NOT: error

// Verify that co_await with complex-returning await_resume compiles.
// Previously hit "emitSuspendExpr Complex" NYI.

namespace std {
template <typename T = void> struct coroutine_handle {
  static coroutine_handle from_address(void *) noexcept;
  void *address() noexcept;
};
template <> struct coroutine_handle<void> {
  static coroutine_handle from_address(void *) noexcept;
  void *address() noexcept;
  template <typename T> coroutine_handle(coroutine_handle<T>) noexcept {}
};
template <typename... T> struct coroutine_traits;
struct suspend_always {
  bool await_ready() noexcept { return false; }
  void await_suspend(coroutine_handle<>) noexcept {}
  void await_resume() noexcept {}
};
}

struct ComplexAwaiter {
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<>) {}
  _Complex float await_resume();
};

struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

template <> struct std::coroutine_traits<Task> {
  using promise_type = Task::promise_type;
};

Task test_complex_await() {
  _Complex float result = co_await ComplexAwaiter{};
  (void)result;
}
