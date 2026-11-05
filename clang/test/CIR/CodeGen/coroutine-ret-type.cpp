// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll
//
// Verify that coroutine ramp functions have ABI-correct return types,
// matching OGCG output. Previously, coroutine functions were skipped
// by the CallConvLowering pass, resulting in un-lowered struct returns.

namespace std {
  template<typename T, typename... Args> struct coroutine_traits;
  template<typename Promise = void> struct coroutine_handle;
  template<> struct coroutine_handle<void> {
    static coroutine_handle from_address(void *addr) noexcept { return {}; }
    void *address() const noexcept { return nullptr; }
  };
  template<typename Promise> struct coroutine_handle : coroutine_handle<void> {
    static coroutine_handle from_address(void *addr) noexcept { return {}; }
  };
  struct suspend_always {
    bool await_ready() const noexcept { return false; }
    void await_suspend(coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
  };
  template<typename Ret, typename... Args>
  struct coroutine_traits { using promise_type = typename Ret::promise_type; };
}

// Empty struct — ABI lowers return to void.
struct EmptyTask {
  struct promise_type {
    EmptyTask get_return_object() noexcept { return {}; }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept {}
  };
};

// Struct with a single int — ABI lowers return to i32.
struct OneIntTask {
  int value;
  struct promise_type {
    OneIntTask get_return_object() noexcept { return {42}; }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept {}
  };
};

// CHECK-LABEL: define dso_local void @_Z10empty_taskv()
EmptyTask empty_task() {
  co_return;
}

// CHECK-LABEL: define dso_local i32 @_Z12one_int_taskv()
OneIntTask one_int_task() {
  co_return;
}
