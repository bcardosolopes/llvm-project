// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir \
// RUN:   -fcxx-exceptions -fexceptions
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
//
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll \
// RUN:   -fcxx-exceptions -fexceptions
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM

// Verify that await_suspend wrapper functions are correctly generated when the
// parent coroutine has active EH cleanup scopes. The wrapper is a separate
// function that must not inherit exception handling state from the coroutine.
// Previously, calls inside the wrapper were incorrectly emitted as try_call
// with cleanup handlers from the coroutine's EH stack, causing cross-function
// value references that violate IsolatedFromAbove on cir.func.

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
template <> struct coroutine_traits<Task> {
  using promise_type = Task::promise_type;
};
}

// RAII type with destructor to create an active EH cleanup scope.
struct Guard {
  Guard();
  ~Guard();
};

// Awaiter whose await_suspend can throw (not noexcept).
struct ThrowingAwaiter {
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<>) {} // can throw
  int await_resume() { return 42; }
};

void use(int);

// The wrapper function should use plain cir.call even though the coroutine
// has an active EH cleanup for the Guard destructor.

// CIR: cir.func always_inline internal private @_Z8test_corv.__await_suspend_wrapper__await
// CIR:   cir.call @_ZN15ThrowingAwaiter13await_suspendE
// CIR-NOT: cir.call exception
// CIR:   cir.return

// The coroutine body itself still needs the standard catch-all wrapper that
// calls promise_type::unhandled_exception(), ends the catch, and then rejoins
// the coroutine final-suspend path.
// CIR-LABEL: cir.func coroutine {{.*}} @_Z8test_corv()
// CIR:      cir.try cleanup {
// CIR:        cir.call exception @_ZN4Task12promise_type11return_voidEv(%{{.*}}) : (!cir.ptr<!rec_Task3A3Apromise_type>) -> () cleanup {
// CIR:      } catch all {
// CIR:        cir.catch_param : !cir.ptr<!void> {cir.end_catch_normal_cleanup}
// CIR:        cir.call exception @_ZN4Task12promise_type19unhandled_exceptionEv(%{{.*}}) : (!cir.ptr<!rec_Task3A3Apromise_type>) -> () cleanup {
// CIR:          cir.catch_param end {cir.end_catch_eh_cleanup}
// CIR:      }
// CIR-NOT:  cir.call @_ZN4Task12promise_type11return_voidEv
// CIR:      cir.call @_ZN4Task12promise_type13final_suspendEv(%{{.*}})

// LLVM: define internal void @_Z8test_corv.__await_suspend_wrapper__await(ptr noundef nonnull %0, ptr noundef %1) #[[WRAPPER_ATTRS:[0-9]+]]
// LLVM:   call void @_ZN15ThrowingAwaiter13await_suspendE
// LLVM:   ret void

// LLVM-LABEL: define dso_local void @_Z8test_corv()
// LLVM:       call ptr @__cxa_begin_catch(ptr %{{.*}})
// LLVM-NEXT:  call void @_ZN4Task12promise_type19unhandled_exceptionEv(ptr noundef nonnull align 1 dereferenceable(1) %{{.*}})
// LLVM:       invoke void @__cxa_end_catch()
// LLVM-NOT:   call void @_ZN4Task12promise_type11return_voidEv
// LLVM:       call void @_ZN4Task12promise_type13final_suspendEv(ptr noundef nonnull align 1 dereferenceable(1) %{{.*}})

// LLVM: attributes #[[WRAPPER_ATTRS]] = {{{.*}}alwaysinline{{.*}}mustprogress{{.*}}}
// OGCG-LABEL: define dso_local void @_Z8test_corv()
// OGCG:       call ptr @__cxa_begin_catch(ptr %{{.*}})
// OGCG-NEXT:  call void @_ZN4Task12promise_type19unhandled_exceptionEv(ptr noundef nonnull align 1 dereferenceable(1) %{{.*}})
// OGCG:       invoke void @__cxa_end_catch()
// OGCG-NOT:   call void @_ZN4Task12promise_type11return_voidEv
// OGCG:       call void @_ZN4Task12promise_type13final_suspendEv(ptr noundef nonnull align 1 dereferenceable(1) %{{.*}})
Task test_cor() {
  Guard g;
  int x = co_await ThrowingAwaiter{};
  use(x);
}
