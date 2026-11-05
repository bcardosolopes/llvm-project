// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s < %t.ll
//
// Regression test: user-written try/catch around co_await with
// co_return inside both the try body and catch handler crashed FlattenCFG.
// The assertion "expect to only find one" yield triggered because
// CallEndCatch::emit emitted a second cir.yield into the dead block after
// co_return's BrOp, in addition to the retBlock's yield from emitReturn.

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

// Test 1: co_return in both try body and catch-all handler.
// Previously crashed with: !yieldOp && "expect to only find one"
Task<int> user_try_catch_coreturn() {
  try {
    co_await std::suspend_always{};
    co_return 1;
  } catch (...) {
    co_return -1;
  }
}

// Verify ramp function calls return_value and final_suspend.
// CHECK: define dso_local void @_Z23user_try_catch_coreturnv()
// CHECK: call void @_ZN4TaskIiE12promise_type12return_valueEi(
// CHECK: call void @_ZN4TaskIiE12promise_type13final_suspendEv(

// Test 2: co_return only in try body, catch handler falls through.
Task<int> user_try_catch_fallthrough() {
  try {
    co_await std::suspend_always{};
    co_return 42;
  } catch (...) {
    // fallthrough — no co_return here
  }
  co_return 0;
}

// CHECK: define dso_local void @_Z26user_try_catch_fallthroughv()
// CHECK: call void @_ZN4TaskIiE12promise_type12return_valueEi(
// CHECK: call void @_ZN4TaskIiE12promise_type13final_suspendEv(
