// Test the behavior of http://wg21.link/P0664, a proposal to catch any
// exceptions thrown after the initial suspend point of a coroutine by
// executing the handler specified by the promise type's 'unhandled_exception'
// member function.
//
// RUN: %clang_cc1 -std=c++20 \
// RUN:   -triple=x86_64-unknown-linux-gnu -emit-llvm -o - %s \
// RUN:   -fexceptions -fcxx-exceptions -disable-llvm-passes \
// RUN:   | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -std=c++20 \
// RUN:   -triple=x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - %s \
// RUN:   -fexceptions -fcxx-exceptions -disable-llvm-passes \
// RUN:   | FileCheck %s --check-prefix=CIR-LLVM %}

#include "Inputs/coroutine.h"

struct throwing_awaitable {
  bool await_ready() { return true; }
  void await_suspend(std::coroutine_handle<>) {}
  void await_resume() { throw 42; }
};

struct throwing_task {
  struct promise_type {
    auto get_return_object() { return throwing_task{}; }
    auto initial_suspend() { return throwing_awaitable{}; }
    auto final_suspend() noexcept { return std::suspend_never{}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

// CHECK-LABEL: define{{.*}} void @_Z1fv()
// CIR-LLVM-LABEL: define dso_local void @_Z1fv() {{.*}} personality ptr @__gxx_personality_v0 {
// CIR-LLVM: %[[RESUMETHREW:.+]] = alloca i8, i64 1, align 1
// CIR-LLVM: invoke void @_ZN13throwing_task12promise_type15initial_suspendEv(
// CIR-LLVM: invoke noundef zeroext i1 @_ZN18throwing_awaitable11await_readyEv(
throwing_task f() {
  // A variable RESUMETHREW is used to keep track of whether the body
  // of 'await_resume' threw an exception. Exceptions thrown in
  // 'await_resume' are unwound to RESUMELPAD.
  // CHECK: init.ready:
  // CHECK-NEXT: store i1 true, ptr %[[RESUMETHREW:.+]], align 1
  // CHECK-NEXT: invoke void @_ZN18throwing_awaitable12await_resumeEv
  // CHECK-NEXT: to label %[[RESUMECONT:.+]] unwind label %[[RESUMELPAD:.+]]

  // If 'await_resume' does not throw an exception, 'false' is stored in
  // variable RESUMETHREW.
  // CHECK: [[RESUMECONT]]:
  // CHECK-NEXT: store i1 false, ptr %[[RESUMETHREW]]
  // CHECK-NEXT: br label %[[RESUMETRYCONT:.+]]

  // 'unhandled_exception' is called for the exception thrown in
  // 'await_resume'. The variable RESUMETHREW is never set to false,
  // and a jump is made to RESUMETRYCONT.
  // CHECK: [[RESUMELPAD]]:
  // CHECK: br label %[[RESUMECATCH:.+]]
  // CHECK: [[RESUMECATCH]]:
  // CHECK: invoke void @_ZN13throwing_task12promise_type19unhandled_exceptionEv
  // CHECK-NEXT: to label %[[RESUMEENDCATCH:.+]] unwind label
  // CHECK: [[RESUMEENDCATCH]]:
  // CHECK-NEXT: invoke void @__cxa_end_catch()
  // CHECK-NEXT: to label %[[RESUMEENDCATCHCONT:.+]] unwind label
  // CHECK: [[RESUMEENDCATCHCONT]]:
  // CHECK-NEXT: br label %[[RESUMETRYCONT]]
  // CHECK: [[RESUMETRYCONT]]:
  // CHECK-NEXT: br label %[[CLEANUP:.+]]
  // CHECK: [[CLEANUP]]:
  // CHECK: switch i32 %{{.+}}, label %{{.+}} [
  // CHECK-NEXT: i32 0, label %[[CLEANUPCONT:.+]]
  // CHECK-NEXT: ]

  // The variable RESUMETHREW is loaded and if true, then 'await_resume'
  // threw an exception and the coroutine body is skipped, and the final
  // suspend is executed immediately. Otherwise, the coroutine body is
  // executed, and then the final suspend.
  // CHECK: [[CLEANUPCONT]]:
  // CHECK-NEXT: %[[RESUMETHREWLOAD:.+]] = load i1, ptr %[[RESUMETHREW]]
  // CHECK-NEXT: br i1 %[[RESUMETHREWLOAD]], label %[[RESUMEDCONT:.+]], label %[[RESUMEDBODY:.+]]

  // CHECK: [[RESUMEDBODY]]:
  // CHECK: invoke void @_ZN13throwing_task12promise_type11return_voidEv
  // CHECK-NEXT: to label %[[REDUMEDBODYCONT:.+]] unwind label
  // CHECK: [[REDUMEDBODYCONT]]:
  // CHECK-NEXT: br label %[[COROFINAL:.+]]

  // CHECK: [[RESUMEDCONT]]:
  // CHECK-NEXT: br label %[[COROFINAL]]

  // CHECK: [[COROFINAL]]:
  // CHECK: call void @_ZN13throwing_task12promise_type13final_suspendEv
  // CIR-LLVM: invoke void @llvm.coro.await.suspend.void(ptr noundef %{{.+}}, ptr noundef %{{.+}}, ptr noundef @_Z1fv.__await_suspend_wrapper__init)
  // CIR-LLVM-NEXT: to label %[[INITSUSPENDCONT:[0-9]+]] unwind label %[[INITSUSPENDLPAD:[a-zA-Z0-9_.]+]]
  // CIR-LLVM: [[INITSUSPENDLPAD]]:
  // CIR-LLVM-NEXT: %{{.+}} = landingpad { ptr, i32 }
  // CIR-LLVM: store i8 1, ptr %[[RESUMETHREW]], align 1
  // CIR-LLVM-NEXT: invoke void @_ZN18throwing_awaitable12await_resumeEv
  // CIR-LLVM-NEXT: to label %[[RESUMECONT:[0-9]+]] unwind label %[[RESUMELPAD:[a-zA-Z0-9_.]+]]
  // CIR-LLVM: [[RESUMECONT]]:
  // CIR-LLVM-NEXT: store i8 0, ptr %[[RESUMETHREW]], align 1
  // CIR-LLVM-NEXT: br label %[[RESUMETRYCONT:[0-9]+]]
  // CIR-LLVM: [[RESUMELPAD]]:
  // CIR-LLVM-NEXT: %{{.+}} = landingpad { ptr, i32 }
  // CIR-LLVM-NEXT: catch ptr null
  // CIR-LLVM: call ptr @__cxa_begin_catch(
  // CIR-LLVM-NEXT: invoke void @_ZN13throwing_task12promise_type19unhandled_exceptionEv
  // CIR-LLVM-NEXT: to label %[[RESUMEENDCATCH:[0-9]+]] unwind label
  // CIR-LLVM: [[RESUMEENDCATCH]]:
  // CIR-LLVM-NEXT: invoke void @__cxa_end_catch()
  // CIR-LLVM-NEXT: to label %[[RESUMETRYCONT:[0-9]+]] unwind label
  // CIR-LLVM: [[RESUMETRYCONT]]:
  // CIR-LLVM-NEXT: call void @llvm.lifetime.end.p0(
  // CIR-LLVM-NEXT: %[[RESUMETHREWLOAD:.+]] = load i8, ptr %[[RESUMETHREW]], align 1
  // CIR-LLVM-NEXT: %[[RESUMETHREWBOOL:.+]] = trunc i8 %[[RESUMETHREWLOAD]] to i1
  // CIR-LLVM-NEXT: br i1 %[[RESUMETHREWBOOL]], label %[[RESUMEDCONT:[0-9]+]], label %[[RESUMEDBODY:[0-9]+]]
  // CIR-LLVM: [[RESUMEDCONT]]:
  // CIR-LLVM-NEXT: br label %[[COROFINAL:[0-9]+]]
  // CIR-LLVM: [[RESUMEDBODY]]:
  // CIR-LLVM: invoke void @_ZN13throwing_task12promise_type11return_voidEv
  // CIR-LLVM-NEXT: to label %[[RESUMEDBODYCONT:[0-9]+]] unwind label
  // CIR-LLVM: [[RESUMEDBODYCONT]]:
  // CIR-LLVM: br label %[[COROFINAL:[0-9]+]]
  // CIR-LLVM: [[COROFINAL]]:
  // CIR-LLVM: call void @_ZN13throwing_task12promise_type13final_suspendEv
  co_return;
}

struct noexcept_awaitable {
  bool await_ready() { return true; }
  void await_suspend(std::coroutine_handle<>) {}
  void await_resume() noexcept {}
};

struct noexcept_task {
  struct promise_type {
    auto get_return_object() { return noexcept_task{}; }
    auto initial_suspend() { return noexcept_awaitable{}; }
    auto final_suspend() noexcept { return std::suspend_never{}; }
    void return_void() {}
    void unhandled_exception() {}
  };
};

// CHECK-LABEL: define{{.*}} void @_Z1gv()
// CIR-LLVM-LABEL: define dso_local void @_Z1gv() {{.*}} personality ptr @__gxx_personality_v0 {
// CIR-LLVM: invoke void @_ZN13noexcept_task12promise_type15initial_suspendEv(
// CIR-LLVM: invoke noundef zeroext i1 @_ZN18noexcept_awaitable11await_readyEv(
noexcept_task g() {
  // If the await_resume function is marked as noexcept, none of the additional
  // conditions that are present in f() above are added to the IR.
  // This means that no i1 are stored before or after calling await_resume:
  // CHECK: init.ready:
  // CHECK-NEXT: call void @_ZN18noexcept_awaitable12await_resumeEv
  // CHECK-NOT: store i1 false, ptr
  // CIR-LLVM: invoke void @llvm.coro.await.suspend.void(ptr noundef %{{.+}}, ptr noundef %{{.+}}, ptr noundef @_Z1gv.__await_suspend_wrapper__init)
  // CIR-LLVM: call void @_ZN18noexcept_awaitable12await_resumeEv
  // CIR-LLVM-NOT: @__cxa_begin_catch
  // CIR-LLVM-NOT: @__cxa_end_catch
  // CIR-LLVM-NOT: store i8 0, ptr
  // CIR-LLVM: invoke void @_ZN13noexcept_task12promise_type11return_voidEv
  co_return;
}
