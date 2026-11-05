// This tests that the coroutine elide optimization could happen succesfully.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O2 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O2 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR-O2 %}
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -O2 -fclangir -disable-llvm-passes -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR-RAW %}

#include "Inputs/coroutine.h"

struct Task {
  struct promise_type {
    struct FinalAwaiter {
      bool await_ready() const noexcept { return false; }
      template <typename PromiseType>
      std::coroutine_handle<> await_suspend(std::coroutine_handle<PromiseType> h) noexcept {
        if (!h)
          return std::noop_coroutine();
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

  Task(std::coroutine_handle<promise_type> handle) : handle(handle) {}
  ~Task() {
    if (handle)
      handle.destroy();
  }

  struct Awaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<void> continuation) noexcept {}
    int await_resume() noexcept {
      return 43;
    }
  };

  auto operator co_await() {
    return Awaiter{};
  }

private:
  std::coroutine_handle<promise_type> handle;
};

Task task0() {
  co_return 43;
}

Task task1() {
  co_return co_await task0();
}

// OGCG-LABEL: define dso_local void @_Z5task1v(
// OGCG: tail call noalias noundef nonnull dereferenceable(40) ptr @_Znwm(i64 noundef 40)

// OGCG-LABEL: define{{.*}} void @_Z5task1v.resume(ptr {{.*}}dereferenceable(40) %0)
// OGCG-NOT: call{{.*}}_Znwm

// OGCG-LABEL: define{{.*}} void @_Z5task1v.destroy(ptr {{.*}}dereferenceable(40) %0)
// OGCG: tail call void @_ZdlPvm(ptr noundef nonnull %0, i64 noundef 40)

// CIR-O2-LABEL: define dso_local void @_Z5task1v(
// CIR-O2: tail call noalias noundef nonnull dereferenceable(40) ptr @_Znwm(i64 noundef 40)

// CIR-O2-LABEL: define{{.*}} void @_Z5task1v.resume(ptr {{.*}}dereferenceable(40) %0)
// CIR-O2-NOT: call{{.*}}_Znwm

// CIR-O2-LABEL: define{{.*}} void @_Z5task1v.destroy(ptr {{.*}}dereferenceable(40) %0)
// CIR-O2: tail call void @_ZdlPvm(ptr noundef nonnull %0, i64 noundef 40)

// CIR-RAW-LABEL: define dso_local void @_Z5task0v(
// CIR-RAW: %[[FREE0:[0-9]+]] = call ptr @llvm.coro.free(token %{{.*}}, ptr %{{.*}})
// CIR-RAW-NEXT: store ptr %[[FREE0]], ptr %[[FREE0_SLOT:[0-9]+]], align 8
// CIR-RAW-NEXT: %[[NONNULL0:[0-9]+]] = icmp ne ptr %[[FREE0]], null
// CIR-RAW-NEXT: br i1 %[[NONNULL0]], label %[[DELETE0:[0-9]+]], label %[[AFTER0:[0-9]+]]
// CIR-RAW: [[DELETE0]]:
// CIR-RAW-NEXT: %[[LOADED_FREE0:[0-9]+]] = load ptr, ptr %[[FREE0_SLOT]], align 8
// CIR-RAW-NEXT: %[[SIZE0:[0-9]+]] = call i64 @llvm.coro.size.i64()
// CIR-RAW-NEXT: call void @_ZdlPvm(ptr noundef %[[LOADED_FREE0]], i64 noundef %[[SIZE0]])

// CIR-RAW-LABEL: define dso_local void @_Z5task1v(
// CIR-RAW: call void @_Z5task0v(ptr dead_on_unwind writable sret(%struct.Task) align 8 %[[TASK_TMP:.*]])
// CIR-RAW: call void @_ZN4TaskawEv(ptr noundef nonnull align 8 dereferenceable(8) %[[TASK_TMP]])
// The destroy edge and the resume continuation meet at a dest-slot selector block,
// which is what routes both into the one copy of the awaited Task's destructor.
// CIR-RAW: i8 1, label %[[DESTROY_EDGE:[0-9]+]]
// CIR-RAW: [[DESTROY_EDGE]]:
// CIR-RAW-NEXT: br label %[[SELECTOR:[0-9]+]]
// CIR-RAW: [[AWAIT_CLEANUP:[0-9]+]]: ; preds = %[[SELECTOR]]
// CIR-RAW-NEXT: call void @_ZN4TaskD1Ev(ptr noundef nonnull align 8 dereferenceable(8) %[[TASK_TMP]]){{.*}}
// CIR-RAW-NEXT: call void @llvm.lifetime.end.p0(ptr %[[TASK_TMP]])
// CIR-RAW-NEXT: call void @llvm.lifetime.end.p0(ptr %{{.*}})
// CIR-RAW: [[SELECTOR]]:
// CIR-RAW-NEXT: %[[SEL:[0-9]+]] = phi i8 [ 0, %{{[0-9]+}} ], [ 1, %[[DESTROY_EDGE]] ]
// CIR-RAW-NEXT: switch i8 %[[SEL]], label %[[AWAIT_CLEANUP]] [
// CIR-RAW: %[[FREE1:[0-9]+]] = call ptr @llvm.coro.free(token %{{.*}}, ptr %{{.*}})
// CIR-RAW-NEXT: store ptr %[[FREE1]], ptr %[[FREE1_SLOT:[0-9]+]], align 8
// CIR-RAW-NEXT: %[[NONNULL1:[0-9]+]] = icmp ne ptr %[[FREE1]], null
// CIR-RAW-NEXT: br i1 %[[NONNULL1]], label %[[DELETE1:[0-9]+]], label %[[AFTER1:[0-9]+]]
// CIR-RAW: [[DELETE1]]:
// CIR-RAW-NEXT: %[[LOADED_FREE1:[0-9]+]] = load ptr, ptr %[[FREE1_SLOT]], align 8
// CIR-RAW-NEXT: %[[SIZE1:[0-9]+]] = call i64 @llvm.coro.size.i64()
// CIR-RAW-NEXT: call void @_ZdlPvm(ptr noundef %[[LOADED_FREE1]], i64 noundef %[[SIZE1]])
