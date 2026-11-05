// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR-FLAT

// Regression test for the symmetric-transfer (SuspendHandle) coroutine
// destroy-cleanup hang. A folly AsyncGenerator co_yield consumed via co_nothrow
// deadlocked under -O1/-O2/-O3 because the body-local destructor on a non-final
// symmetric-transfer suspend's i8==1 destroy edge was emitted as a destroy-only
// side-path branching straight to the frame-dealloc tail; after the trivial
// destructor inlined, LLVM simplifycfg merged that copy with the coroutine's
// EH-unwind destructor copies into a multi-suspend-reachable shape that LLVM
// CoroSplit could not partition cleanly into the .resume/.destroy clones.
//
// The fix routes BOTH the i8==1 destroy edge and the resume continuation of
// such a suspend through a shared dest-slot SELECTOR block (block-argument
// selector), mirroring classic CodeGen's cleanup.dest.slot model, so the
// destructor block is dominated by a block that is also on the resume path and
// CoroSplit const-folds the selector per clone (0 in .resume DCEs the
// destructor; 1 in .destroy keeps it).
//
// This guards the IR shape: a regression back to the destroy-only side-path
// would manifest only as a non-deterministic runtime deadlock under
// optimization, which the rest of the lit suite would not catch.

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
  static coroutine_handle from_promise(Promise &) { return {}; }
};
struct suspend_always {
  bool await_ready() const noexcept { return false; }
  void await_suspend(coroutine_handle<>) const noexcept {}
  void await_resume() const noexcept {}
};
}

// A SuspendHandle awaiter: await_suspend returns a coroutine_handle<>
// (symmetric transfer), the same shape folly::coro::AsyncGenerator's co_yield
// lowers to.
struct HandleAwaiter {
  bool await_ready() noexcept { return false; }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept {
    return std::coroutine_handle<>::from_address(nullptr);
  }
  void await_resume() noexcept {}
};

struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};
namespace std {
template <> struct coroutine_traits<Task> { using promise_type = Task::promise_type; };
}

struct Guard {
  Guard();
  ~Guard();
};

// `g` is a body-local with a non-trivial destructor live across a NON-FINAL
// symmetric-transfer suspend -- exactly the shape that previously hung.

// CIR-FLAT-LABEL: cir.func coroutine {{.*}}@_Z1fv()
//
// The i8==1 destroy edge selects 1 and branches to the dest-slot selector block:
// CIR-FLAT:      cir.const #cir.int<1> : !u8i
// CIR-FLAT-NEXT: cir.br ^[[SEL:bb[0-9]+]](
//
// The resume continuation selects 0 and reaches the SAME selector block (so the
// destructor block is not a destroy-only side-path):
// CIR-FLAT:      cir.const #cir.int<0> : !u8i
// CIR-FLAT-NEXT: cir.br ^[[SEL]](
//
// The body-local destructor is on the destroy side, reachable from the selector:
// CIR-FLAT:      cir.call @_ZN5GuardD1Ev
//
// The selector block dispatches on its u8 block argument:
// CIR-FLAT:      ^[[SEL]]({{.*}}: !u8i
// CIR-FLAT-NEXT: cir.switch.flat
Task f() {
  Guard g;
  co_await HandleAwaiter{};
}
