// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
//
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.flat.cir
// RUN: FileCheck %s --input-file=%t.flat.cir --check-prefix=FLAT
//
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM

// Test symmetric transfer: await_suspend returns coroutine_handle<>.
// CIR emits __builtin_coro_resume in the suspend region.
// FlattenCFG wraps it with coro.save/coro.suspend.
// LLVM lowers __builtin_coro_resume to llvm.coro.resume.

namespace std {
  template<typename T, typename... Args>
  struct coroutine_traits;

  template<typename Promise = void>
  struct coroutine_handle;

  template<>
  struct coroutine_handle<void> {
    static coroutine_handle from_address(void *addr) noexcept { return {}; }
    void *address() const noexcept { return nullptr; }
    void resume() const {}
    void destroy() const {}
    bool done() const { return false; }
  };

  template<typename Promise>
  struct coroutine_handle : coroutine_handle<void> {
    static coroutine_handle from_address(void *addr) noexcept { return {}; }
    static coroutine_handle from_promise(Promise &promise) { return {}; }
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
    void return_void() {}
    void unhandled_exception() {}

    // Symmetric transfer: final_suspend returns coroutine_handle<>.
    struct final_awaiter {
      bool await_ready() const noexcept { return false; }
      std::coroutine_handle<> await_suspend(
          std::coroutine_handle<promise_type>) noexcept {
        return std::coroutine_handle<>::from_address(nullptr);
      }
      void await_resume() const noexcept {}
    };
    final_awaiter final_suspend() noexcept { return {}; }
  };
};

namespace std {
  template<>
  struct coroutine_traits<Task> {
    using promise_type = Task::promise_type;
  };
}

// CIR-LABEL: cir.func coroutine
// CIR-SAME:  @_Z21symmetric_transfer_fnv
//
// Final suspend — symmetric transfer: await_suspend returns coroutine_handle<>.
// The suspend region calls __builtin_coro_await_suspend_handle via a wrapper.
// CIR:         cir.await(final,
// CIR:         suspend : {
// CIR:           cir.cast bitcast %{{.*}} -> !cir.ptr<!void>
// CIR:           cir.get_global @_Z21symmetric_transfer_fnv.__await_suspend_wrapper__final
// CIR:           cir.call @__builtin_coro_await_suspend_handle(
// CIR-NEXT:      cir.yield
//
// Flat CIR — FlattenCFG injects coro.save/coro.suspend around the suspend body.
// FLAT-LABEL: cir.func coroutine
// FLAT-SAME:  @_Z21symmetric_transfer_fnv
// FLAT:         cir.call @__builtin_coro_save(
// FLAT:         cir.call @__builtin_coro_await_suspend_handle(
// FLAT:         cir.call @__builtin_coro_suspend(
//
// LLVM: call fastcc void %{{.*}}(ptr %{{.*}})

Task symmetric_transfer_fn() {
  co_return;
}
