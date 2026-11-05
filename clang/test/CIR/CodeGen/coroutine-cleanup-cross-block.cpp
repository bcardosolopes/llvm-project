// RUN: %clang_cc1 -std=c++20 -triple aarch64-none-linux-android26 -fclangir -emit-llvm %s -o %t.ll \
// RUN:   -fcxx-exceptions -fexceptions -Oz
// RUN: FileCheck %s --input-file=%t.ll
//
// Verify that coroutine cleanup blocks whose values are used by reachable
// destructor calls are not erroneously eliminated during FlattenCFG's
// dead block pruning. Previously, a cleanup block with no CFG predecessors
// (but with values used by a reachable block) would be erased by
// dropAllDefinedValueUses(), nulling the operand and causing a crash in
// the CIR->LLVM conversion fold infrastructure.

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
template <typename... Args> struct coroutine_traits<Task, Args...> {
  using promise_type = Task::promise_type;
};
}

// A nontrivially-destructible wrapper that creates cleanup edges.
struct Wrapper {
  int *p;
  Wrapper(int *x) : p(x) {}
  ~Wrapper();
  int get() const { return *p; }
};

// An awaitable that holds a Wrapper — its destructor must run in the
// cleanup path, creating a cross-block value use.
struct AwaitableWithCleanup {
  Wrapper w;
  AwaitableWithCleanup(int *x) : w(x) {}
  ~AwaitableWithCleanup();
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<>) {}
  int await_resume() { return w.get(); }
};

void use(int);

// This function creates a scenario where the awaitable's destructor
// cleanup block can become disconnected from the CFG during flattening.
Task coroutine_with_awaitable_cleanup(int *data) {
  AwaitableWithCleanup a(data);
  int val = co_await a;
  use(val);
}

// CHECK: define {{.*}} @_Z32coroutine_with_awaitable_cleanupPi
// Just verify it compiles without crashing.
// CHECK: ret void
