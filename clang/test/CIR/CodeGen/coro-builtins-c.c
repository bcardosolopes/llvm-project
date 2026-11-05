// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c23 -fclangir \
// RUN:   -fcoroutines -emit-cir -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c23 -fclangir \
// RUN:   -fcoroutines -emit-llvm -disable-llvm-passes -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Test that C coroutine builtins (__builtin_coro_*) compile through CIR
// without assertion crashes. The C coroutine builtins are used outside a
// C++ coroutine body, so CIR must set up CoroData when __builtin_coro_id
// is called.

void *myAlloc(long long);

// CIR-LABEL: @f
// CIR:         %[[COROID:.+]] = cir.call @__builtin_coro_id
// CIR:         %[[ALLOC:.+]] = cir.call @__builtin_coro_alloc(%[[COROID]])
// CIR:         %[[BEGIN:.+]] = cir.call @__builtin_coro_begin(%[[COROID]],
// CIR:         cir.call @__builtin_coro_resume(%[[BEGIN]])
// CIR:         cir.call @__builtin_coro_destroy(%[[BEGIN]])
// CIR:         cir.call @__builtin_coro_done(%[[BEGIN]])
// CIR:         cir.call @__builtin_coro_free(%[[COROID]], %[[BEGIN]])
// CIR:         cir.call @__builtin_coro_end(%[[BEGIN]],

// LLVM-LABEL: define dso_local void @f(
// LLVM-SAME:  ) #[[FATTR:[0-9]+]] {
// LLVM:         %[[COROID:.+]] = call token @llvm.coro.id(i32 32,
// LLVM:         call i1 @llvm.coro.alloc(token %[[COROID]])
// LLVM:         call ptr @llvm.coro.noop()
// LLVM:         %[[BEGIN:.+]] = call ptr @llvm.coro.begin(token %[[COROID]],
// LLVM:         call void @llvm.coro.resume(ptr %[[BEGIN]])
// LLVM:         call void @llvm.coro.destroy(ptr %[[BEGIN]])
// LLVM:         call i1 @llvm.coro.done(ptr %[[BEGIN]])
// LLVM:         call ptr @llvm.coro.free(token %[[COROID]], ptr %[[BEGIN]])
// LLVM:         call void @llvm.coro.end(ptr %[[BEGIN]], i1 false, token none)
// LLVM:         call i8 @llvm.coro.suspend(token none, i1 true)
// LLVM-NEXT:    ret void
// LLVM:         attributes #[[FATTR]] = { noinline nounwind optnone "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-features"="+cx8,+mmx,+sse,+sse2,+x87" }
void f(int n) {
  int promise;
  __builtin_coro_id(32, &promise, 0, 0);
  __builtin_coro_alloc();
  __builtin_coro_noop();
  __builtin_coro_begin(myAlloc(__builtin_coro_size()));
  __builtin_coro_resume(__builtin_coro_frame());
  __builtin_coro_destroy(__builtin_coro_frame());
  __builtin_coro_done(__builtin_coro_frame());
  __builtin_coro_free(__builtin_coro_frame());
  __builtin_coro_end(__builtin_coro_frame(), 0);
  __builtin_coro_suspend(1);
}
