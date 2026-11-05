// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
//
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM

// Verify that co_await inside a ternary expression does not produce a
// cross-function SSA reference.  The await_suspend wrapper is a separate
// cir.func; when emitted while the outer coroutine is inside a conditional
// evaluation (the ternary), the wrapper must not inherit the parent's
// outermostConditional context.  Previously, createCleanupActiveFlag()
// would allocate the cleanup.cond flag in the wrapper's entry block but
// then store to the parent's conditional insertion point, creating a
// 'cir.store' that uses a value defined outside its region.

namespace std {
template <typename T, typename... Args> struct coroutine_traits;
template <typename Promise = void> struct coroutine_handle;
template <> struct coroutine_handle<void> {
  static coroutine_handle from_address(void *addr) noexcept { return {}; }
  void *address() const noexcept { return nullptr; }
};
template <typename Promise>
struct coroutine_handle : coroutine_handle<void> {
  static coroutine_handle from_address(void *addr) noexcept { return {}; }
};
struct suspend_always {
  bool await_ready() const noexcept { return false; }
  void await_suspend(coroutine_handle<>) const noexcept {}
  void await_resume() const noexcept {}
};
}

struct MyData {
  int x;
  MyData() : x(0) {}
  MyData(int v) : x(v) {}
};

struct Task {
  struct promise_type {
    MyData value;
    Task get_return_object() { return {}; }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_value(MyData v) { value = v; }
    void unhandled_exception() {}
  };

  struct awaiter {
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<>) {}
    MyData await_resume() { return MyData{42}; }
  };
  awaiter operator co_await() { return {}; }
};

namespace std {
template <> struct coroutine_traits<Task, bool, MyData> {
  using promise_type = Task::promise_type;
};
}

Task fetchData();

// The coroutine function and its await_suspend wrapper must each be
// self-contained: no SSA value defined in one may be used by the other.
// CIR-LABEL: cir.func coroutine {{.*}}@_Z7exampleb6MyData
// CIR: cir.return

// LLVM-LABEL: define {{.*}} @_Z7exampleb6MyData
// LLVM: ret
Task example(bool hasPrefetched, MyData prefetched) {
  MyData result = hasPrefetched
      ? prefetched
      : co_await fetchData();
  co_return result;
}
