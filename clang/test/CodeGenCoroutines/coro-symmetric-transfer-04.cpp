// This tests that the symmetric transfer at the final suspend point could happen successfully.
// Based on https://github.com/llvm/llvm-project/pull/85271#issuecomment-2007554532
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O2 -emit-llvm %s -o - | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O2 -disable-llvm-passes -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR-PRESPLIT %}
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O2 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR-O2 %}

#include "Inputs/coroutine.h"

struct Task {
  struct promise_type {
    struct FinalAwaiter {
      bool await_ready() const noexcept { return false; }
      template <typename PromiseType>
      std::coroutine_handle<> await_suspend(std::coroutine_handle<PromiseType> h) noexcept {
        return h.promise().continuation;
      }
      void await_resume() noexcept {}
    };
    Task get_return_object() noexcept {
      return std::coroutine_handle<promise_type>::from_promise(*this);
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    FinalAwaiter final_suspend() noexcept { return {}; }
    void unhandled_exception() noexcept {}
    void return_value(int x) noexcept {
      _value = x;
    }
    std::coroutine_handle<> continuation;
    int _value;
  };

  Task(std::coroutine_handle<promise_type> handle) : handle(handle), stuff(123) {}

  struct Awaiter {
    std::coroutine_handle<promise_type> handle;
    Awaiter(std::coroutine_handle<promise_type> handle) : handle(handle) {}
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<void> await_suspend(std::coroutine_handle<void> continuation) noexcept {
      handle.promise().continuation = continuation;
      return handle;
    }
    int await_resume() noexcept {
      int ret = handle.promise()._value;
      handle.destroy();
      return ret;
    }
  };

  auto operator co_await() {
    auto handle_ = handle;
    handle = nullptr;
    return Awaiter(handle_);
  }

private:
  std::coroutine_handle<promise_type> handle;
  int stuff;
};

Task task0() {
  co_return 43;
}

// CHECK-LABEL: define{{.*}} void @_Z5task0v.resume
// This checks we are still in the scope of the current function.
// CHECK-NOT: {{^}}}
// CHECK: musttail call fastcc void
// CHECK-NEXT: ret void

// CIR-PRESPLIT-LABEL: define dso_local { ptr, i32 } @_Z5task0v()
// CIR-PRESPLIT: switch i8 %{{.*}}, label %[[INIT_SUSP_RET:[^ ]+]] [
// CIR-PRESPLIT: i8 1, label %[[INIT_CLEANUP:[^ ]+]]
// CIR-PRESPLIT: [[INIT_CLEANUP]]:
// CIR-PRESPLIT: call void @llvm.lifetime.end.p0
// CIR-PRESPLIT: br label %[[DESTROY:[^ ]+]]
// CIR-PRESPLIT: [[INIT_SUSP_RET]]:
// CIR-PRESPLIT: br label %[[RETBLOCK:[^ ]+]]
// CIR-PRESPLIT: switch i8 %{{.*}}, label %[[FINAL_SUSP_RET:[^ ]+]] [
// CIR-PRESPLIT: i8 1, label %[[FINAL_CLEANUP:[^ ]+]]
// CIR-PRESPLIT: [[FINAL_CLEANUP]]:
// CIR-PRESPLIT: br label %[[FINAL_AFTER:[^ ]+]]
// CIR-PRESPLIT: [[FINAL_SUSP_RET]]:
// CIR-PRESPLIT: br label %[[RETBLOCK]]
// CIR-PRESPLIT: [[FINAL_AFTER]]:
// CIR-PRESPLIT: call void @llvm.lifetime.end.p0
// CIR-PRESPLIT: br label %[[DESTROY]]
// CIR-PRESPLIT: [[DESTROY]]:
// CIR-PRESPLIT: call void @_ZdlPvm
// CIR-PRESPLIT: br label %[[RETBLOCK]]
// CIR-PRESPLIT: [[RETBLOCK]]:
// CIR-PRESPLIT: call void @llvm.coro.end(ptr null, i1 false, token none)
// CIR-PRESPLIT-NOT: ret { ptr, i32 } undef
// CIR-PRESPLIT: %[[RETVAL:.+]] = load { ptr, i32 }, ptr %{{.+}}, align 8
// CIR-PRESPLIT: ret { ptr, i32 } %[[RETVAL]]

// CIR-O2-LABEL: define dso_local { ptr, i32 } @_Z5task0v()
// CIR-O2: store ptr @_Z5task0v.resume
// CIR-O2: store ptr @_Z5task0v.destroy
// CIR-O2-NOT: @_ZdlPvm
// CIR-O2: ret { ptr, i32 } {{.*}}

// CIR-O2-LABEL: define internal fastcc void @_Z5task0v.resume
// CIR-O2: musttail call fastcc void
// CIR-O2-NEXT: ret void

// CIR-O2-LABEL: define internal fastcc void @_Z5task0v.destroy
// CIR-O2: tail call void @_ZdlPvm
// CIR-O2-NEXT: ret void
