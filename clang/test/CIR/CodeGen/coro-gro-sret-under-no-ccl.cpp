// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN: -fclangir-mem2reg -O2 -emit-llvm \
// RUN:     %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test: under the Android ingestion mode
// (-fno-clangir-call-conv-lowering) the call-conv lowering pass that rewrites a
// by-value aggregate return into an sret argument is deferred to the
// direct-to-LLVM pipeline. Before the fix, the CIR mem2reg pass still ran early
// in the CIR-to-CIR pipeline and promoted the coroutine get_return_object
// (`__retval`) slot to SSA *before* the deferred CallConvLowering could turn the
// return into an sret store -- so the store into the return slot was silently
// dropped and the caller received a garbage coroutine handle (resume() crashed).
// The fix defers mem2reg with call-conv lowering, so the ramp function stores
// the coroutine handle into the sret argument.

namespace std {
template <typename T, typename...> struct coroutine_traits {
  using promise_type = typename T::promise_type;
};
template <typename Promise = void> struct coroutine_handle;
template <> struct coroutine_handle<void> {
  static coroutine_handle from_address(void *addr) noexcept {
    coroutine_handle me;
    me.ptr = addr;
    return me;
  }
  void *address() const noexcept { return ptr; }
  void resume() const { __builtin_coro_resume(ptr); }
  void destroy() const { __builtin_coro_destroy(ptr); }
  bool done() const { return __builtin_coro_done(ptr); }
  coroutine_handle(decltype(nullptr)) : ptr(nullptr) {}
  coroutine_handle() : ptr(nullptr) {}
  explicit operator bool() const { return ptr; }

protected:
  void *ptr;
};
template <typename Promise> struct coroutine_handle : coroutine_handle<> {
  using coroutine_handle<>::operator=;
  static coroutine_handle from_address(void *addr) noexcept {
    coroutine_handle me;
    me.ptr = addr;
    return me;
  }
  Promise &promise() const {
    return *reinterpret_cast<Promise *>(
        __builtin_coro_promise(ptr, alignof(Promise), false));
  }
  static coroutine_handle from_promise(Promise &promise) {
    coroutine_handle p;
    p.ptr = __builtin_coro_promise(&promise, alignof(Promise), true);
    return p;
  }
};
struct suspend_always {
  bool await_ready() noexcept { return false; }
  void await_suspend(coroutine_handle<>) noexcept {}
  void await_resume() noexcept {}
};
} // namespace std

// A return object that holds the coroutine handle (returned by value / sret).
struct Gen {
  struct promise_type {
    int cur;
    Gen get_return_object() {
      return Gen{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(int v) {
      cur = v;
      return {};
    }
    void return_void() {}
    void unhandled_exception() {}
  };
  std::coroutine_handle<promise_type> h;
  // A user-declared destructor makes Gen non-trivially destructible, so the
  // Itanium ABI returns it indirectly (via an sret argument) -- exercising the
  // by-value-aggregate-return path this test guards.
  explicit Gen(std::coroutine_handle<promise_type> x) : h(x) {}
  ~Gen() {
    if (h)
      h.destroy();
  }
};

Gen count(int n) {
  for (int i = 0; i < n; i++)
    co_yield i;
}

// The ramp function returns Gen by sret (first pointer argument). The coroutine
// frame pointer must be stored through that sret argument, not into a dead local
// alloca that gets optimized away.

// CHECK-LABEL: define {{.*}}void @_Z5counti(
// CHECK-SAME:      ptr {{.*}}sret(%struct.Gen){{.*}} [[RET:%[0-9]+]], i32 {{.*}})
// CHECK:         [[FRAME:%[0-9]+]] = {{.*}}call {{.*}}@_Znwm(
// CHECK:         store ptr [[FRAME]], ptr [[RET]]
