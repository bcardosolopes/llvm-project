// __builtin_coro_size / __builtin_coro_align are lowered to the llvm.coro.size /
// llvm.coro.align intrinsics by the direct-to-LLVM coro-builtin interception,
// which only fires for plain cir.call ops. They were previously emitted via
// emitCall, which (a) subjects the callee to the call-vs-invoke decision -- so
// an active EH cleanup turned it into an invoke that the invoke lowering path
// does not intercept -- and (b) subjects it to -fsanitize=function CFI
// instrumentation, which takes the callee's address. Both left a dangling
// reference to the (nonexistent) __builtin_coro_size / __builtin_coro_align
// symbol and caused link failures.
//
// The fix emits these two builtins as plain named cir.call ops like every other
// coro builtin, so they are always lowered to the llvm.coro.* intrinsic and no
// __builtin_coro_* symbol is ever referenced.
//
// -fsanitize=function is the check inside -fsanitize=undefined that triggered
// the CFI address-take on the coro-size callee.
//
// Pre-CoroSplit lowering: the frame size is computed via the llvm.coro.size
// intrinsic (never an invoke, never a CFI check of __builtin_coro_size).
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-nopass.ll \
// RUN:   -fcxx-exceptions -fexceptions -fsanitize=function
// RUN: FileCheck %s --input-file=%t-nopass.ll --check-prefix=NOPASS
//
// Full pipeline: the module is valid and there is no dangling reference to any
// __builtin_coro_* symbol (which would be an undefined external at link time).
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll \
// RUN:   -fcxx-exceptions -fexceptions -fsanitize=function
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM

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
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};
namespace std {
template <> struct coroutine_traits<Task> {
  using promise_type = Task::promise_type;
};
}

// The frame size comes from the llvm.coro.size intrinsic (a plain call), and the
// coro-size callee is never invoked or CFI-checked via a __builtin_coro_size
// operand.
// NOPASS-LABEL: define dso_local void @_Z4corov()
// NOPASS:         call i64 @llvm.coro.size.i64()
// NOPASS-NOT:     invoke {{.*}}@__builtin_coro_size
// NOPASS-NOT:     getelementptr {{.*}}@__builtin_coro_size

// After the full pipeline the module exists (compilation succeeded) and no
// __builtin_coro_* symbol survives to become an undefined external at link.
// LLVM-LABEL: define dso_local void @_Z4corov()
// LLVM-NOT:   @__builtin_coro_size
// LLVM-NOT:   @__builtin_coro_align

Task coro() {
  int local = 0;
  (void)local;
  co_return;
}
