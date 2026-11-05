// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll \
// RUN:   -fcxx-exceptions -fexceptions
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM
//
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll \
// RUN:   -fcxx-exceptions -fexceptions
// RUN: FileCheck %s --input-file=%t.og.ll --check-prefix=OGCG

// Verify that a coroutine with a function-try-block emits exactly one
// fallthrough coro.end (isUnwind=false). Previously, both the try body and
// catch handler paths emitted their own fallthrough coro.end, causing
// "Only one coro.end can be marked as fallthrough" in CoroSplit.

namespace std {
template <typename R, typename...> struct coroutine_traits {
  using promise_type = typename R::promise_type;
};
template <typename Promise = void> struct coroutine_handle;
template <> struct coroutine_handle<void> {
  static coroutine_handle from_address(void *addr) noexcept {
    coroutine_handle me;
    me.ptr = addr;
    return me;
  }
  void operator()() { resume(); }
  void *address() const { return ptr; }
  void resume() const { __builtin_coro_resume(ptr); }
  void destroy() const { __builtin_coro_destroy(ptr); }
  bool done() const { return __builtin_coro_done(ptr); }
  void *ptr;
};
template <typename Promise> struct coroutine_handle : coroutine_handle<> {};
struct suspend_never {
  bool await_ready() noexcept { return true; }
  void await_suspend(coroutine_handle<>) noexcept {}
  void await_resume() noexcept {}
};
} // namespace std

struct task {
  struct promise_type {
    task get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() noexcept {}
  };
};

task f() try {
  co_return;
} catch (...) {
}

// Verify CoroSplit succeeds (no "Only one coro.end can be marked as
// fallthrough" crash) by checking for the split coroutine functions.
// LLVM: define {{.*}} @_Z1fv(
// LLVM: define internal fastcc void @_Z1fv.resume(
// LLVM: define internal fastcc void @_Z1fv.destroy(
// LLVM: define internal fastcc void @_Z1fv.cleanup(

// OGCG: define {{.*}} @_Z1fv(
// OGCG: define internal fastcc void @_Z1fv.resume(
// OGCG: define internal fastcc void @_Z1fv.destroy(
// OGCG: define internal fastcc void @_Z1fv.cleanup(
