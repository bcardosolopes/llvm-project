// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -fclangir -emit-llvm %s \
// RUN:   -fcxx-exceptions -fexceptions -std=c++20 -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify that co_return inside a catch handler does not crash FlattenCFG
// with "expect to only find one" yield assertion. co_return in a coroutine
// catch handler creates a dead fallthrough block with a stale yield, which
// caused FlattenCFG to assert when it found two yields.

namespace std {
template <typename T> struct coroutine_handle {
  static coroutine_handle from_address(void *) noexcept;
  void *address() const noexcept;
};
template <> struct coroutine_handle<void> {
  template <typename PromiseType>
  coroutine_handle(coroutine_handle<PromiseType>) noexcept;
  static coroutine_handle from_address(void *) noexcept;
  void *address() const noexcept;
};
struct suspend_always {
  bool await_ready() noexcept;
  void await_suspend(coroutine_handle<void>) noexcept;
  void await_resume() noexcept;
};
template <typename Ret, typename... Args> struct coroutine_traits {
  using promise_type = typename Ret::promise_type;
};
} // namespace std

struct Task {
  struct promise_type {
    Task get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

struct MyError {};

void may_throw();
void log_error(const MyError &);

// Test 1: co_return inside a typed catch handler
Task test_typed_catch() {
  try {
    may_throw();
  } catch (const MyError &e) {
    log_error(e);
    co_return;
  }
  co_return;
}

// CHECK-LABEL: @_Z16test_typed_catchv.resume
// CHECK: call ptr @__cxa_begin_catch
// CHECK: invoke void @_Z9log_errorRK7MyError
// CHECK: call void @_ZN4Task12promise_type11return_voidEv
// CHECK: invoke void @__cxa_end_catch()
// CHECK-NEXT: to label %{{.+}} unwind label %{{.+}}

// Test 2: co_return inside a catch-all handler
Task test_catchall() {
  try {
    may_throw();
  } catch (...) {
    co_return;
  }
  co_return;
}

// CHECK-LABEL: @_Z13test_catchallv.resume
// CHECK: call ptr @__cxa_begin_catch
// CHECK: call void @_ZN4Task12promise_type11return_voidEv
// CHECK: invoke void @__cxa_end_catch()
// CHECK-NEXT: to label %{{.+}} unwind label %{{.+}}
