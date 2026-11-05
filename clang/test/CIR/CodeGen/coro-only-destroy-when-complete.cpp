// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:     -fclangir -emit-llvm -disable-llvm-passes %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 \
// RUN:     -emit-llvm -disable-llvm-passes %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

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

struct A;
struct A_promise_type {
  A get_return_object();
  std::suspend_always initial_suspend();
  std::suspend_always final_suspend() noexcept;
  void return_value(int);
  void unhandled_exception();
};

struct Awaitable {
  bool await_ready();
  int await_resume();
  template <typename F>
  void await_suspend(F);
};
Awaitable something();

struct [[clang::coro_only_destroy_when_complete]] A {
  using promise_type = A_promise_type;
  A();
  A(std::coroutine_handle<>);
  ~A();
  std::coroutine_handle<A_promise_type> handle;
};

A foo() {
  co_await something();
  co_return 43;
}

// CHECK: define{{.*}} @_Z3foov({{.*}}) #[[ATTR_NUM:[0-9]+]]
// CHECK: attributes #[[ATTR_NUM]] = {{.*}}coro_only_destroy_when_complete

// OGCG: define{{.*}} @_Z3foov({{.*}}) #[[ATTR_NUM:[0-9]+]]
// OGCG: attributes #[[ATTR_NUM]] = {{.*}}coro_only_destroy_when_complete
