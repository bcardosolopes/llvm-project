// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O2 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// A body-local object whose destructor is live
// across a plain (SuspendVoid) suspend point must be destroyed on the coroutine
// destroy edge (handle.destroy() while suspended). The fix carries those
// destructors in an optional 4th `cleanup` region on cir.await; FlattenCFG then
// routes the LLVM CoroSplit .destroy clone through it. A regression that dropped
// the destroy-edge dtor would remove the `cleanup` region (CIR) and the .destroy
// clone's dtor call (LLVM), failing these CHECKs.

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

// A body-local type with a nontrivial destructor.
struct Guard {
  ~Guard();
};

struct Generator {
  struct promise_type {
    Generator get_return_object();
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(int) noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept {}
  };
};

// `g` is live across the co_yield (SuspendVoid) suspend point, so the yield
// await must carry a cleanup region that destroys it on the destroy edge.
Generator gen() {
  Guard g;
  co_yield 1;
}

// The init suspend has no live body locals, so it must NOT gain a cleanup
// region (its await closes at `},)` with no `cleanup :`).
// CIR-LABEL: cir.func coroutine {{.*}} @_Z3genv()
// CIR:         cir.await(init, ready : {
// CIR:         }, suspend : {
// CIR:         }, resume : {
// CIR-NOT:       cleanup :
// CIR:         },)

// The co_yield await must carry the extra `cleanup` region, and that region
// must contain the body-local Guard destructor call. A regression that dropped
// the destroy-edge cleanup would omit this region.
// CIR:         cir.await(yield, ready : {
// CIR:         }, suspend : {
// CIR:         }, resume : {
// CIR:         }, cleanup : {
// CIR:           cir.call @_ZN5GuardD1Ev(
// CIR:           cir.yield
// CIR:         },)

// The final suspend likewise has no live body locals and must NOT gain a
// cleanup region.
// CIR:         cir.await(final, ready : {
// CIR:         }, suspend : {
// CIR:         }, resume : {
// CIR-NOT:       cleanup :
// CIR:         },)

// After lowering + CoroSplit, the .destroy clone runs the Guard destructor on
// the destroy edge.
// LLVM: define internal fastcc void @_Z3genv.destroy(
// LLVM:   call void @_ZN5GuardD1Ev(
