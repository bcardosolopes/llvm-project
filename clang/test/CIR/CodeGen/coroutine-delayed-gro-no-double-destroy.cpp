// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Regression test for the delayed get-return-object (GRO) double-destroy. When
// the coroutine's return type differs from get_return_object()'s type, the
// GRO->return converting constructor is now emitted in the ramp's
// __coro_destroy path, is_in_ramp-guarded and BEFORE the coroutine-body
// GRO/promise cleanups. If the converting ctor throws, the GRO temporary and
// promise must each be destroyed exactly once (previously twice). Runtime
// proof: a throwing-ctor repro yields groDtor=2/promiseDtor=1, matching classic
// CodeGen.

namespace std {
template <typename T, typename... Args> struct coroutine_traits;
template <typename Promise = void> struct coroutine_handle;
template <> struct coroutine_handle<void> {
  static coroutine_handle from_address(void *addr) noexcept { return {}; }
  void *address() const noexcept { return nullptr; }
};
template <typename Promise> struct coroutine_handle : coroutine_handle<void> {
  static coroutine_handle from_address(void *addr) noexcept { return {}; }
  static coroutine_handle from_promise(Promise &) { return {}; }
};
struct suspend_never {
  bool await_ready() const noexcept { return true; }
  void await_suspend(coroutine_handle<>) const noexcept {}
  void await_resume() const noexcept {}
};
} // namespace std

struct Gro { int t; Gro(int); Gro(const Gro &); ~Gro(); };
struct Task {
  int v;
  Task(Gro g);
  struct promise_type {
    ~promise_type();
    Gro get_return_object();
    std::suspend_never initial_suspend() noexcept;
    std::suspend_never final_suspend() noexcept;
    void return_void();
    void unhandled_exception();
  };
};
namespace std {
template <> struct coroutine_traits<Task> {
  using promise_type = Task::promise_type;
};
} // namespace std

Task run() { co_return; }

// The delayed-GRO conversion is emitted in the ramp, is_in_ramp-guarded, and
// invokes the Task(Gro) converting constructor:
// CHECK-LABEL: cir.func{{.*}}@_Z3runv
// CHECK: cir.call @__builtin_coro_is_in_ramp
// CHECK: cir.{{(try_)?}}call @_ZN3GroC1ERKS_
// CHECK: cir.{{(try_)?}}call @_ZN4TaskC1E3Gro
