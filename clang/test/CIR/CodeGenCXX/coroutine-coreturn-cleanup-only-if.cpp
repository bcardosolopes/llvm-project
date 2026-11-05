// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj %s -o %t.o
//
// Regression test: this coroutine previously triggered
// "empty block: expect at least a terminator" in CIR verification.
// The crash happened when co_return was emitted inside the simple-if fast path
// (cleanup-only LexicalScope), leaving a return block in that scope that never
// got a terminator.

namespace std {
template <typename T, typename... Args>
struct coroutine_traits;

template <typename Promise = void>
struct coroutine_handle;

template <>
struct coroutine_handle<void> {
  static coroutine_handle from_address(void *) noexcept { return {}; }
  void *address() const noexcept { return nullptr; }
  void resume() const {}
  void destroy() const {}
  bool done() const { return false; }
};

template <typename Promise>
struct coroutine_handle : coroutine_handle<void> {
  static coroutine_handle from_address(void *addr) noexcept { return {}; }
  static coroutine_handle from_promise(Promise &) { return {}; }
};

struct suspend_always {
  bool await_ready() const noexcept { return false; }
  void await_suspend(coroutine_handle<>) const noexcept {}
  void await_resume() const noexcept {}
};

struct suspend_never {
  bool await_ready() const noexcept { return true; }
  void await_suspend(coroutine_handle<>) const noexcept {}
  void await_resume() const noexcept {}
};
} // namespace std

template <typename T>
struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_value(T) {}
    void unhandled_exception() {}
  };
};

namespace std {
template <typename... Args>
struct coroutine_traits<Task<int>, Args...> {
  using promise_type = Task<int>::promise_type;
};
} // namespace std

Task<int> test_cleanup_only_if_coreturn(bool c, bool d) {
  auto l = [=]() -> Task<int> {
    do {
      if (c)
        co_return 1;
      if (d)
        co_return 2;
    } while (false);
    co_return 3;
  };
  return l();
}
