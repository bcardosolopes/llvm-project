// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.flat.cir
// RUN: FileCheck %s --input-file=%t.flat.cir --check-prefix=FLAT
//
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM

// Multi-suspend coroutine: verifies that each co_yield gets its own
// coro.save/coro.suspend pair, and CoroSplit processes all suspension points.

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
  struct suspend_never {
    bool await_ready() const noexcept { return true; }
    void await_suspend(coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
  };
}

struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
    std::suspend_always yield_value(int) { return {}; }
  };
};

namespace std {
  template<> struct coroutine_traits<Task> {
    using promise_type = Task::promise_type;
  };
}

// Flat-CIR: each co_yield gets a coro.save/coro.suspend pair with the
// await_suspend call going through a wrapper + intrinsic.
//
// FLAT-LABEL: cir.func coroutine
// FLAT-SAME:  @_Z13multi_suspendv
//
// Initial suspend (suspend_never):
// FLAT:         cir.call @__builtin_coro_save(
// FLAT:         cir.call @__builtin_coro_await_suspend_void(
// FLAT:         cir.call @__builtin_coro_suspend(
//
// First co_yield (suspend_always):
// FLAT:         cir.call @_ZN4Task12promise_type11yield_valueEi(
// FLAT:         cir.call @__builtin_coro_save(
// FLAT:         cir.call @__builtin_coro_await_suspend_void(
// FLAT:         cir.call @__builtin_coro_suspend(
//
// Second co_yield (suspend_always):
// FLAT:         cir.call @_ZN4Task12promise_type11yield_valueEi(
// FLAT:         cir.call @__builtin_coro_save(
// FLAT:         cir.call @__builtin_coro_await_suspend_void(
// FLAT:         cir.call @__builtin_coro_suspend(
//
// Final suspend (suspend_always):
// FLAT:         cir.call @__builtin_coro_save(
// FLAT:         cir.call @__builtin_coro_await_suspend_void(
// FLAT:         cir.call @__builtin_coro_suspend(
//
// FLAT:         cir.call @__builtin_coro_end(

// LLVM-LABEL: define dso_local void @_Z13multi_suspendv()
//
// CoroSplit creates multiple suspension points:
// LLVM:       CoroSave:
// LLVM:       CoroSuspend:
// LLVM:       CoroSave{{[0-9]+}}:
// LLVM:       CoroSuspend{{[0-9]+}}:
// LLVM:       CoroSave{{[0-9]+}}:
// LLVM:       CoroSuspend{{[0-9]+}}:
// LLVM:       CoroSave{{[0-9]+}}:
// LLVM:       CoroSuspend{{[0-9]+}}:
// LLVM:       CoroEnd:

Task multi_suspend() {
  co_yield 1;
  co_yield 2;
}
