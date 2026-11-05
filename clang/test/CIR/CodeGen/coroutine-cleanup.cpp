// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll \
// RUN:   -fcxx-exceptions -fexceptions
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
//
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-ogcg.ll \
// RUN:   -fcxx-exceptions -fexceptions -fno-clangir-direct-lowering
// RUN: FileCheck %s --input-file=%t-ogcg.ll --check-prefix=OGCG

// Verify that local variable destructors (RAII guards) are correctly emitted
// on the co_return path. Previously, co_return created a branch to a return
// block inside the scope, and the scope's cleanup code (destructor calls) was
// emitted into a dead block that was subsequently erased, causing the
// destructor to be missing from the generated code.
//
// Also verify that the normal co_return path reaches final_suspend before
// coroutine teardown. Previously, the try body scope emitted cir.return
// (exiting the function) instead of cir.yield (exiting the try body),
// causing the normal path to skip final_suspend entirely.

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

struct VoidTask {
  struct promise_type {
    VoidTask get_return_object() { return {}; }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};
namespace std {
template <> struct coroutine_traits<VoidTask> {
  using promise_type = VoidTask::promise_type;
};
}

struct Guard {
  Guard();
  ~Guard();
};

void use(int);

// Test: explicit co_return with RAII guard must call Guard destructor.
// The Guard destructor must appear in the resume function.

// Check that the resume function calls the Guard destructor AND final_suspend.
// The correct order per [dcl.fct.def.coroutine]/5 is:
//   return_void → Guard dtor → final_suspend → coro teardown
//
// LLVM-LABEL: define {{.*}} @_Z13test_coreturnv.resume(
// LLVM: call void @_ZN5GuardD1Ev(
// LLVM: call {{.*}} @_ZN8VoidTask12promise_type13final_suspendEv(
// LLVM-NOT: call void @llvm.trap
// LLVM: }

// OGCG-LABEL: define {{.*}} @_Z13test_coreturnv.resume(
// OGCG: call void @_ZN5GuardD1Ev(
// OGCG: call {{.*}} @_ZN8VoidTask12promise_type13final_suspendEv(
// OGCG-NOT: call void @llvm.trap
// OGCG: }

VoidTask test_coreturn() {
  Guard g;
  co_await std::suspend_always{};
  co_return;
}
