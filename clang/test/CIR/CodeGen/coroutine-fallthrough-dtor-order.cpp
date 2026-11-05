// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Regression test: on the IMPLICIT fall-off path of a coroutine body (no
// explicit co_return) that has a suspend point, the body's automatic locals
// must be destroyed BEFORE promise.return_void() -- matching classic CodeGen
// and g++. ClangIR previously emitted return_void() first, then the local
// destructors (an observable side-effect reordering). After the fix,
// return_void() is immediately followed by the branch to the final-suspend
// label; pre-fix the ~Guard cleanup was emitted between return_void() and that
// branch. (Runtime A/B vs classic and g++ confirms body/Guard.dtor/return_void
// ordering across -O0/-O2/-O3.)

namespace std {
template <typename T, typename...> struct coroutine_traits;
template <typename P = void> struct coroutine_handle;
template <> struct coroutine_handle<void> {
  static coroutine_handle from_address(void *) noexcept { return {}; }
  void *address() const noexcept { return nullptr; }
};
template <typename P> struct coroutine_handle : coroutine_handle<void> {
  static coroutine_handle from_address(void *) noexcept { return {}; }
  static coroutine_handle from_promise(P &) { return {}; }
};
struct suspend_always {
  bool await_ready() const noexcept { return false; }
  void await_suspend(coroutine_handle<>) const noexcept {}
  void await_resume() const noexcept {}
};
} // namespace std

struct Guard { ~Guard(); };
struct Task {
  struct promise_type {
    Task get_return_object();
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void();
    void unhandled_exception();
  };
};
namespace std {
template <> struct coroutine_traits<Task> {
  using promise_type = Task::promise_type;
};
} // namespace std

Task coro() {
  Guard g;
  co_await std::suspend_always{};
}

// CHECK-LABEL: cir.func{{.*}}@_Z4corov
// CHECK: cir.call @_ZN5GuardD1Ev
// CHECK: cir.call @_ZN4Task12promise_type11return_voidEv
// CHECK-NEXT: cir.goto "{{.*}}.__coro_final"
