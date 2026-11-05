// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test coroutine with get_return_object_on_allocation_failure (allocation
// failure handling). When the coroutine frame allocation returns null, the
// coroutine returns the result of get_return_object_on_allocation_failure()
// instead of proceeding with the coroutine body.

namespace std {
template <class Ret, typename... T>
struct coroutine_traits { using promise_type = typename Ret::promise_type; };

template <class Promise = void>
struct coroutine_handle {
  static coroutine_handle from_address(void *) noexcept;
};
template <>
struct coroutine_handle<void> {
  template <class PromiseType>
  coroutine_handle(coroutine_handle<PromiseType>) noexcept;
  static coroutine_handle from_address(void *);
};

struct nothrow_t {};
constexpr nothrow_t nothrow = {};

struct suspend_never {
  bool await_ready() noexcept { return true; }
  void await_suspend(coroutine_handle<>) noexcept {}
  void await_resume() noexcept {}
};
} // namespace std

// Required when get_return_object_on_allocation_failure() is defined by
// the promise.
using SizeT = decltype(sizeof(int));
void *operator new(SizeT __sz, const std::nothrow_t &) noexcept;
void operator delete(void *__p, const std::nothrow_t &) noexcept;

struct Task {
  struct promise_type {
    Task get_return_object() { return {}; }
    static Task get_return_object_on_allocation_failure() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

Task alloc_failure_coro() {
  co_return;
}

// CHECK: cir.func coroutine {{.*}} @_Z18alloc_failure_corov()
// CHECK:   cir.if
// CHECK:     %[[ALLOC:.*]] = cir.call @_ZnwmRKSt9nothrow_t
// CHECK:     %[[IS_NULL:.*]] = cir.cmp(eq, %[[ALLOC]]
// CHECK:     cir.if %[[IS_NULL]] {
// CHECK:       cir.return
// CHECK:     }
// CHECK:     cir.store{{.*}} %[[ALLOC]]
// CHECK:   }
