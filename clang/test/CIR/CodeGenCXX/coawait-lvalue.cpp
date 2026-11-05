// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 %s -o %t.ll
// co_await expression as l-value emission (CoawaitExprClass in emitLValue).

namespace std {
  template<class Promise = void>
  struct coroutine_handle {
    static coroutine_handle from_address(void *addr) noexcept;
    void *address() const noexcept;
    operator coroutine_handle<>() const noexcept;
  };
  template<>
  struct coroutine_handle<void> {
    static coroutine_handle from_address(void *addr) noexcept;
    void *address() const noexcept;
  };
  template <class R, class... Args>
  struct coroutine_traits {
    using promise_type = typename R::promise_type;
  };
  struct suspend_always {
    bool await_ready() const noexcept { return false; }
    void await_suspend(coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
  };
}

struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

struct Awaitable {
  int value;
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<>) {}
  int& await_resume() { return value; }
};

Task test() {
  Awaitable a;
  int& ref = co_await a;
  ref = 42;
}
