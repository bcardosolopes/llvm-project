// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Regression test: when get_return_object()'s type (Inner) differs from the
// coroutine's declared return type (Ret) -- a delayed GRO needing a converting
// constructor -- the conversion must run on the ramp for EVERY ramp exit,
// including when the coroutine suspends at initial/final suspend. CIRGen emits
// the is_in_ramp-guarded conversion only on the fallthrough-completion path (in
// the __coro_destroy block); FlattenCFG now also clones it onto each
// coroutine suspend-return-to-caller edge (matching classic CodeGen's
// EmitGroConv at Coro.SuspendBB). Pre-fix there was a single conversion on the
// fallthrough path, so a suspend_always coroutine skipped it (wrong return
// value / swallowed converting-ctor exception). After the fix the flattened IR
// contains the Ret(Inner) converting ctor on multiple ramp-exit edges.

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

struct Inner { int v; };
struct Ret { int v; Ret(Inner); };
struct Task {
  struct promise_type {
    Inner get_return_object();
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void();
    void unhandled_exception();
  };
};
namespace std {
template <> struct coroutine_traits<Ret> {
  using promise_type = Task::promise_type;
};
} // namespace std

Ret coro() { co_return; }

// The delayed-GRO conversion Ret(Inner) is emitted on more than one ramp-exit
// edge (fallthrough completion + the cloned suspend-return edge(s)), not just
// once on the fallthrough path.
// CHECK-LABEL: cir.func{{.*}}@_Z4corov
// CHECK-COUNT-2: cir.{{(try_)?}}call @_ZN3RetC1E5Inner
