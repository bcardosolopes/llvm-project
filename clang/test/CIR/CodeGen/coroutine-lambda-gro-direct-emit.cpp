// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR

// Regression test for a coroutine-lambda get_return_object (GRO) miscompile.
//
// When the coroutine return type equals get_return_object()'s type, CIRGen must
// take the "direct emit" GRO path: emit get_return_object() eagerly into the
// return slot, sequenced before initial_suspend (per [dcl.fct.def.coroutine]p7).
//
// The bug: emitCoroutineBody computed the function return type via
// curFuncDecl->getReturnType(). For a coroutine-lambda's operator(), curFuncDecl
// is getNonClosureContext() -- the *enclosing* function, whose return type is
// unrelated (here void). hasSameType() then failed, spuriously selecting the
// delayed-GRO path. That path re-emits get_return_object() into the sret return
// slot in a coro.is_in_ramp-guarded block at the end of the coroutine (after all
// suspends), making the sret pointer live across every suspend so LLVM CoroSplit
// spills it into the heap coroutine frame -- corrupting the returned object once
// the frame outlives the ramp. For an always-suspending coroutine the ramp also
// returns on the suspend path without ever initializing the sret, so the
// returned object's coroutine handle is garbage.
//
// The fix compares against curFnRetTy (the arranged return type of the function
// actually being emitted), matching OGCG's use of CGF.FnRetTy. A lambda
// coroutine must now behave exactly like a named coroutine: a single, eager GRO
// call sequenced before initial_suspend and no coro.is_in_ramp guard.

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

struct suspend_always {
  bool await_ready() noexcept { return false; }
  void await_suspend(coroutine_handle<>) noexcept {}
  void await_resume() noexcept {}
};

} // namespace std

struct Task {
  struct promise_type {
    Task get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void() noexcept;
    void unhandled_exception() noexcept;
  };
};

// Named coroutine oracle: this case always took the correct direct-emit path.
// The lambda below must produce the same GRO shape (this is the no-regression
// / OGCG-equivalence anchor).
Task named_coro() {
  co_await std::suspend_always();
}

// CIR-LABEL: cir.func coroutine {{.*}}@_Z10named_corov
// CIR:         cir.call @__builtin_coro_begin
// CIR:         cir.call @_ZN4Task12promise_type17get_return_objectEv
// CIR-NOT:     cir.call @_ZN4Task12promise_type17get_return_objectEv
// CIR:         cir.call @_ZN4Task12promise_type15initial_suspendEv
// CIR-NOT:     cir.call @__builtin_coro_is_in_ramp
// CIR:         cir.return

// Immediately-invoked capturing coroutine-lambda temporary -- the pervasive
// async-scope idiom (e.g. scope.detached_spawn_on(sched, [this]() -> task {
// co_await ...; }())). The closure's operator() is the coroutine; before the
// fix it spuriously took the delayed-GRO path and corrupted the returned task.
void make() {
  [] () -> Task {
    co_await std::suspend_always();
  }();
}

// The lambda's call operator is a coroutine. Its GRO must be direct-emitted:
// one eager get_return_object() call, sequenced before initial_suspend, and no
// coro.is_in_ramp guard anywhere in the function body -- identical to the named
// coroutine above.

// CIR-LABEL: cir.func coroutine {{.*}}lambda{{.*}}@_ZZ4makevENK3$_0clEv
// CIR:         cir.call @__builtin_coro_begin
// CIR:         cir.call @_ZN4Task12promise_type17get_return_objectEv
// CIR-NOT:     cir.call @_ZN4Task12promise_type17get_return_objectEv
// CIR:         cir.call @_ZN4Task12promise_type15initial_suspendEv
// CIR-NOT:     cir.call @__builtin_coro_is_in_ramp
// CIR:         cir.return
