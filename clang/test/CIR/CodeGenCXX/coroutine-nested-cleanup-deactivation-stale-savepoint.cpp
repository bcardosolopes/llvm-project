// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-obj %s -o %t.o
//
// Regression test for stale cleanup-deactivation savepoints.
//
// Nested cleanup scopes can pop inner cleanups before an outer
// CleanupDeactivationScope force-deactivates deferred entries. That used to
// trigger:
//   EHScopeStack::find(stable_iterator): finding savepoint after pop
//
// This coroutine shape (throw in branch + cleanup wrappers) exercises the same
// control-flow pattern without depending on external repro files.

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

struct D {
  ~D();
};
struct A {
  D d;
};
A mk();

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

Task<int> repro_stale_cleanup_savepoint(bool c) {
  if (c)
    throw mk();
  co_return 1;
}
