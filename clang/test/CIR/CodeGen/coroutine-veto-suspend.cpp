// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.flat.cir
// RUN: FileCheck --input-file=%t.flat.cir %s -check-prefix=FLAT

// Test coroutine with bool-returning await_suspend (veto suspension pattern).
// When await_suspend returns false, the coroutine skips suspension and goes
// directly to await_resume.

namespace std {
template <class Ret, typename... T>
struct coroutine_traits { using promise_type = typename Ret::promise_type; };

template <class Promise = void>
struct coroutine_handle {
  static coroutine_handle from_address(void *) noexcept;
};
template <>
struct coroutine_handle<void> {
  template <class PromiseType>
  coroutine_handle(coroutine_handle<PromiseType>) noexcept;
  static coroutine_handle from_address(void *);
};

struct suspend_always {
  bool await_ready() noexcept { return false; }
  void await_suspend(coroutine_handle<>) noexcept {}
  void await_resume() noexcept {}
};
} // namespace std

struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

// Awaiter with bool-returning await_suspend (veto pattern).
struct VetoAwaiter {
  bool await_ready() { return false; }
  bool await_suspend(std::coroutine_handle<>) { return true; }
  void await_resume() {}
};

Task test_veto() {
  co_await VetoAwaiter{};
}

// Check that CIR uses __builtin_coro_await_suspend_bool + cir.condition in the
// suspend region for the veto awaiter.
// CIR: cir.func coroutine {{.*}} @_Z9test_vetov()
// CIR:   cir.await(user, ready : {
// CIR:     cir.condition(
// CIR:   }, suspend : {
// CIR:     cir.cast bitcast %{{.*}} : !cir.ptr<!rec_VetoAwaiter> -> !cir.ptr<!void>
// CIR:     cir.get_global @_Z9test_vetov.__await_suspend_wrapper__await
// CIR:     %[[VETO:.*]] = cir.call @__builtin_coro_await_suspend_bool(
// CIR:     cir.condition(%[[VETO]])
// CIR:   }, resume : {
// CIR:     cir.call @_ZN11VetoAwaiter12await_resumeEv
// CIR:     cir.yield
// CIR:   },)

// Check that FlattenCFG produces a conditional branch for the veto.
// In flat CIR, the veto bool should gate whether coro.suspend is called.
// FLAT: cir.func coroutine {{.*}} @_Z9test_vetov()
// FLAT:   cir.call @__builtin_coro_await_suspend_bool(
// FLAT:   cir.brcond
