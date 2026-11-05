// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=CIR
//
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.flat.cir
// RUN: FileCheck %s --input-file=%t.flat.cir --check-prefix=FLAT
//
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll --check-prefix=LLVM

// Coroutine lowering: cir.await regions are properly flattened by FlattenCFG
// and lowered to LLVM IR. FlattenCFG injects coro.save/coro.suspend intrinsics
// into the suspend region, with a three-way branch on the suspend result
// (0=resume, 1=cleanup, default=suspend-return).
//
// CIR verifies: coroutine attribute on cir.func, __builtin_coro_* calls,
// cir.await regions before flattening.
// FLAT verifies: coro.save/coro.suspend injection by FlattenCFG.
// LLVM verifies: coroutine intrinsics are lowered and LLVM's CoroSplit pass
// runs (consuming presplitcoroutine), producing CoroEnd/AfterCoroEnd blocks.

namespace std {
  template<typename T, typename... Args>
  struct coroutine_traits;

  template<typename Promise = void>
  struct coroutine_handle;

  template<>
  struct coroutine_handle<void> {
    static coroutine_handle from_address(void *addr) noexcept { return {}; }
    void *address() const noexcept { return nullptr; }
    void resume() const {}
    void destroy() const {}
    bool done() const { return false; }
  };

  template<typename Promise>
  struct coroutine_handle : coroutine_handle<void> {
    static coroutine_handle from_address(void *addr) noexcept { return {}; }
    static coroutine_handle from_promise(Promise &promise) { return {}; }
  };

  struct suspend_always {
    bool await_ready() const noexcept { return false; }
    void await_suspend(coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
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
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

namespace std {
  template<>
  struct coroutine_traits<Task> {
    using promise_type = Task::promise_type;
  };
}

// CIR:       cir.func coroutine
// CIR-SAME:  @_Z16simple_coroutinev
// CIR:         cir.call @__builtin_coro_id(
// CIR:         cir.call @__builtin_coro_alloc(
// CIR:         cir.call @__builtin_coro_begin(
// CIR:         cir.await(init,
// CIR:         cir.call @_ZN4Task12promise_type11return_voidEv(
// CIR:         cir.await(final,
// CIR:         cir.call @__builtin_coro_free(

// Flat-CIR -- FlattenCFG injects coro.save/coro.suspend into suspend regions.
//
// FLAT-LABEL: cir.func coroutine
// FLAT-SAME:  @_Z16simple_coroutinev
//
// Initial suspend — coro.save(null), wrapper via __builtin_coro_await_suspend_void, coro.suspend(save, false):
// FLAT:         cir.call @__builtin_coro_save(
// FLAT:         cir.call @__builtin_coro_await_suspend_void(
// FLAT:         cir.call @__builtin_coro_suspend(
//
// Body:
// FLAT:         cir.call @_ZN4Task12promise_type11return_voidEv(
//
// Final suspend — coro.save(null), wrapper via __builtin_coro_await_suspend_void, coro.suspend(save, true):
// FLAT:         cir.call @__builtin_coro_save(
// FLAT:         cir.call @__builtin_coro_await_suspend_void(
// FLAT:         cir.call @__builtin_coro_suspend(
//
// FLAT:         cir.call @__builtin_coro_end(

// LLVM-LABEL: define dso_local void @_Z16simple_coroutinev()
//
// Initial suspend (suspend_never) — CoroSplit processes coro.save/coro.suspend:
// LLVM:         call noundef zeroext i1 @_ZNKSt13suspend_never11await_readyEv(
// LLVM:         br i1 %{{.*}}, label %[[INIT_RESUME:.*]], label %CoroSave
//
// LLVM:       CoroSave:
// LLVM:       AfterCoroSave:
// LLVM:         call void @_Z16simple_coroutinev.__await_suspend_wrapper__init(
// LLVM:       CoroSuspend:
//
// LLVM:       [[INIT_RESUME]]:
// LLVM:         call void @_ZNKSt13suspend_never12await_resumeEv(
//
// Body:
// LLVM:         call void @_ZN4Task12promise_type11return_voidEv(
//
// Final suspend (suspend_always) — CoroSplit with final suspend handling:
// LLVM:         call noundef zeroext i1 @_ZNKSt14suspend_always11await_readyEv(
// LLVM:         br i1 %{{.*}}, label %[[FINAL_RESUME:.*]], label %CoroSave{{[0-9]+}}
//
// LLVM:       CoroSave{{[0-9]+}}:
// LLVM:       AfterCoroSave{{[0-9]+}}:
// LLVM:         call void @_Z16simple_coroutinev.__await_suspend_wrapper__final(
// LLVM:       CoroSuspend{{[0-9]+}}:
//
// LLVM:       [[FINAL_RESUME]]:
// LLVM:         call void @_ZNKSt14suspend_always12await_resumeEv(
//
// Coroutine end (produced by CoroSplit pass consuming presplitcoroutine):
// LLVM:       CoroEnd:

Task simple_coroutine() {
  co_return;
}
