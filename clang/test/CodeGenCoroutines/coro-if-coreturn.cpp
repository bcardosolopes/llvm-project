// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG
// RUN: %if cir-support %{ %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=CIR %}

#include "Inputs/coroutine.h"

struct coroutine {
  struct promise_type {
    coroutine get_return_object();
    std::suspend_never initial_suspend() noexcept;
    std::suspend_always final_suspend() noexcept;
    void return_void();
    void unhandled_exception();
  };
};

int foo();
void use();

// OGCG-LABEL: define dso_local void @_Z1hv()
// OGCG: call noundef i32 @_Z3foov()
// OGCG: br i1 %{{.+}}, label %[[RET:[^,]+]], label %[[CONT:[^,]+]]
// OGCG: [[RET]]:
// OGCG-NEXT: call void @_ZN9coroutine12promise_type11return_voidEv
// OGCG-NEXT: br label %[[FINAL:[^,]+]]
// OGCG: [[CONT]]:
// OGCG: call void @_Z3usev()
// OGCG: call void @_ZN9coroutine12promise_type11return_voidEv
// OGCG-NEXT: br label %[[FINAL]]
// OGCG: [[FINAL]]:
// OGCG: call void @_ZN9coroutine12promise_type13final_suspendEv

// ClangIR gives both exits one shared return_void block; classic CodeGen emits a
// copy per exit. Both call it exactly once. The orders only diverge when the body
// has locals to destroy, and there ClangIR matches classic.
// CIR-LABEL: define dso_local void @_Z1hv()
// CIR: call noundef i32 @_Z3foov()
// CIR: br i1 %{{.+}}, label %[[RET:[^,]+]], label %[[CONT:[^,]+]]
// CIR: [[CONT]]:
// CIR-NEXT: call void @_Z3usev()
// CIR-NEXT: br label %[[RET]]
// CIR: [[RET]]:
// CIR-NEXT: call void @_ZN9coroutine12promise_type11return_voidEv
// CIR-NEXT: br label %[[FINAL:[^,]+]]
// CIR: [[FINAL]]:
// CIR: call void @_ZN9coroutine12promise_type13final_suspendEv
coroutine h() {
  if (foo())
    co_return;
  use();
}
