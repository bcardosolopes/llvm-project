// RUN: %clang_cc1 -std=c++20 -fcoroutines -fcxx-exceptions -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null

namespace std {
template <class... Args>
struct coroutine_traits;

template <class Promise = void>
struct coroutine_handle;

template <>
struct coroutine_handle<void> {
  static coroutine_handle from_address(void *) noexcept { return {}; }
  void *address() const noexcept { return nullptr; }
};

template <class Promise>
struct coroutine_handle : coroutine_handle<void> {
  static coroutine_handle from_address(void *addr) noexcept { return {}; }
  static coroutine_handle from_promise(Promise &) { return {}; }
};
} // namespace std

struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    struct awaiter {
      bool await_ready() const noexcept { return true; }
      void await_suspend(std::coroutine_handle<>) const noexcept {}
      void await_resume() const noexcept {}
    };
    awaiter initial_suspend() noexcept { return {}; }
    awaiter final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

namespace std {
template <>
struct coroutine_traits<Task> {
  using promise_type = Task::promise_type;
};
} // namespace std

struct Thrower {
  ~Thrower() noexcept(false) {}
};

void may_throw() { throw 1; }

Task f() {
  Thrower t;
  may_throw();
  co_return;
}
