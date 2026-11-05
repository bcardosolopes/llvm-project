// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm %s -o - -disable-llvm-passes | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o - -disable-llvm-passes | FileCheck %s --check-prefix=CIR-LLVM %}

#include "Inputs/coroutine.h"

using namespace std;

struct coro {
  struct promise_type {
    coro get_return_object();
    suspend_always initial_suspend();
    suspend_never final_suspend() noexcept;
    void return_void();
    static void unhandled_exception();
  };
};

extern "C" coro f(int) { co_return; }
// Verify that cleanup.dest.slot is eliminated in a coroutine.
// CHECK-LABEL: f(
// CHECK: %[[INIT_SUSPEND:.+]] = call i8 @llvm.coro.suspend(
// CHECK-NEXT: switch i8 %[[INIT_SUSPEND]], label
// CHECK-NEXT:   i8 0, label %[[INIT_READY:.+]]
// CHECK-NEXT:   i8 1, label %[[INIT_CLEANUP:.+]]
// CHECK-NEXT: ]
// CHECK: %[[CLEANUP_DEST0:.+]] = phi i32 [ 0, %[[INIT_READY]] ], [ 2, %[[INIT_CLEANUP]] ]

// CHECK: %[[FINAL_SUSPEND:.+]] = call i8 @llvm.coro.suspend(
// CHECK-NEXT: switch i8 %{{.*}}, label %coro.ret [
// CHECK-NEXT:   i8 0, label %[[FINAL_READY:.+]]
// CHECK-NEXT:   i8 1, label %[[FINAL_CLEANUP:.+]]
// CHECK-NEXT: ]

// CHECK: call void @_ZNSt13suspend_never12await_resumeEv(
// CHECK: %[[CLEANUP_DEST1:.+]] = phi i32 [ 0, %[[FINAL_READY]] ], [ 2, %[[FINAL_CLEANUP]] ]
// CHECK: %[[CLEANUP_DEST2:.+]] = phi i32 [ %[[CLEANUP_DEST0]], %{{.+}} ], [ %[[CLEANUP_DEST1]], %{{.+}} ], [ 0, %{{.+}} ]
// CHECK: call ptr @llvm.coro.free(
// CHECK: switch i32 %[[CLEANUP_DEST2]], label %{{.+}} [
// CHECK-NEXT: i32 0
// CHECK-NEXT: i32 2
// CHECK-NEXT: ]

// CIR-LLVM-LABEL: define dso_local void @f(
// CIR-LLVM: %[[INIT_SUSPEND:[0-9]+]] = call i8 @llvm.coro.suspend(
// CIR-LLVM-NEXT: switch i8 %[[INIT_SUSPEND]], label %[[INIT_SUSP_RET:[0-9]+]] [
// CIR-LLVM-NEXT:   i8 0, label %[[INIT_READY:[0-9]+]]
// CIR-LLVM-NEXT:   i8 1, label %[[INIT_CLEANUP:[0-9]+]]
// CIR-LLVM-NEXT: ]
// CIR-LLVM: {{^}}[[INIT_CLEANUP]]:
// CIR-LLVM-NEXT: call void @llvm.lifetime.end.p0(ptr %{{.+}})
// CIR-LLVM-NEXT: br label %[[DESTROY:[0-9]+]]
// CIR-LLVM: {{^}}[[INIT_SUSP_RET]]:
// CIR-LLVM-NEXT: br label %[[SUSP_RET:[0-9]+]]
// CIR-LLVM: %[[FINAL_SUSPEND:[0-9]+]] = call i8 @llvm.coro.suspend(
// CIR-LLVM-NEXT: switch i8 %[[FINAL_SUSPEND]], label %[[FINAL_SUSP_RET:[0-9]+]] [
// CIR-LLVM-NEXT:   i8 0, label %[[FINAL_READY:[0-9]+]]
// CIR-LLVM-NEXT:   i8 1, label %[[FINAL_CLEANUP:[0-9]+]]
// CIR-LLVM-NEXT: ]
// CIR-LLVM: {{^}}[[FINAL_SUSP_RET]]:
// CIR-LLVM-NEXT: br label %[[SUSP_RET]]
// CIR-LLVM: {{^}}[[DESTROY]]:
// CIR-LLVM-NEXT: call void @llvm.lifetime.end.p0(ptr %{{.+}})
// CIR-LLVM-NEXT: call void @llvm.lifetime.end.p0(ptr %{{.+}})
// CIR-LLVM-NEXT: %[[FREE:[0-9]+]] = call ptr @llvm.coro.free(
// CIR-LLVM-NEXT: store ptr %[[FREE]], ptr %{{.+}}, align 8
// CIR-LLVM-NEXT: %[[HASFREE:[0-9]+]] = icmp ne ptr %[[FREE]], null
// CIR-LLVM-NEXT: br i1 %[[HASFREE]], label %[[DELETE:[0-9]+]], label %[[SUSP_RET]]
// CIR-LLVM: {{^}}[[DELETE]]:
// CIR-LLVM-NEXT: %{{.+}} = load ptr, ptr %{{.+}}, align 8
// CIR-LLVM-NEXT: %{{.+}} = call i64 @llvm.coro.size.i64()
// CIR-LLVM-NEXT: call void @_ZdlPvm(
// CIR-LLVM-NEXT: br label %[[SUSP_RET]]
// CIR-LLVM: {{^}}[[SUSP_RET]]:
// CIR-LLVM-NOT: @llvm.coro.free
// CIR-LLVM-NOT: @_ZdlPvm
// CIR-LLVM: call void @llvm.coro.end(ptr null, i1 false, token none)
// CIR-LLVM-NEXT: ret void
