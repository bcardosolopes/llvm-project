// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
//
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Verify that mixed co_await expressions with different awaiter return types
// generate correctly named wrapper functions and matching intrinsic calls.

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
struct suspend_always {
  bool await_ready() const noexcept { return false; }
  void await_suspend(coroutine_handle<>) const noexcept {}
  void await_resume() const noexcept {}
};
}

struct BoolAwaiter {
  bool await_ready() { return false; }
  bool await_suspend(std::coroutine_handle<>) { return true; }
  int await_resume() { return 42; }
};

struct VoidAwaiter {
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<>) {}
  void await_resume() {}
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
template <> struct coroutine_traits<Task> {
  using promise_type = Task::promise_type;
};
}

// CIR: cir.func always_inline internal private @_Z14mixed_awaitersv.__await_suspend_wrapper__init
// CIR: cir.func always_inline internal private @_Z14mixed_awaitersv.__await_suspend_wrapper__await
// CIR: cir.func always_inline internal private @_Z14mixed_awaitersv.__await_suspend_wrapper__await2
// CIR-SAME: -> (!cir.bool
// CIR: cir.func always_inline internal private @_Z14mixed_awaitersv.__await_suspend_wrapper__await3
// CIR: cir.func always_inline internal private @_Z14mixed_awaitersv.__await_suspend_wrapper__final

// LLVM: define internal void @_Z14mixed_awaitersv.__await_suspend_wrapper__init(ptr noundef nonnull %0, ptr noundef %1) #[[INIT_ATTRS:[0-9]+]]
// LLVM: define internal void @_Z14mixed_awaitersv.__await_suspend_wrapper__await(ptr noundef nonnull %0, ptr noundef %1) #[[AWAIT_ATTRS:[0-9]+]]
// LLVM: define internal {{.*}}i1 @_Z14mixed_awaitersv.__await_suspend_wrapper__await2(ptr noundef nonnull %0, ptr noundef %1) #[[AWAIT_ATTRS]]
// LLVM: define internal void @_Z14mixed_awaitersv.__await_suspend_wrapper__await3(ptr noundef nonnull %0, ptr noundef %1) #[[AWAIT_ATTRS]]
// LLVM: define internal void @_Z14mixed_awaitersv.__await_suspend_wrapper__final(ptr noundef nonnull %0, ptr noundef %1) #[[INIT_ATTRS]]

// Init/final wrappers call noexcept suspend_always methods, so they get nounwind.
// Await wrappers call non-noexcept VoidAwaiter/BoolAwaiter methods, so they don't.
// LLVM: attributes #[[INIT_ATTRS]] = {{{.*}}alwaysinline{{.*}}mustprogress{{.*}}nounwind{{.*}}}
// LLVM: attributes #[[AWAIT_ATTRS]] = {{{.*}}alwaysinline{{.*}}mustprogress{{.*}}}
Task mixed_awaiters() {
  VoidAwaiter va;
  co_await va;
  int x = co_await BoolAwaiter{};
  co_await va;
}
