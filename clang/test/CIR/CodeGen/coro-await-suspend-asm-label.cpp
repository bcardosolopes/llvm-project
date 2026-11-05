// RUN: %clang_cc1 -std=gnu++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=gnu++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -fcxx-exceptions -fexceptions -fclangir -emit-llvm \
// RUN:   -disable-llvm-passes %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -std=gnu++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -fcxx-exceptions -fexceptions -emit-llvm -disable-llvm-passes %s \
// RUN:   -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

extern "C" void my_await_suspend_void(void *, void *, void *)
    asm("__builtin_coro_await_suspend_void");
extern "C" bool my_await_suspend_bool(void *, void *, void *)
    asm("__builtin_coro_await_suspend_bool");
extern "C" void *my_await_suspend_handle(void *, void *, void *)
    asm("__builtin_coro_await_suspend_handle");

extern "C" void test_coro_await_suspend_void_asm_label(void *a, void *b,
                                                       void *c) {
  try {
    my_await_suspend_void(a, b, c);
  } catch (...) {
  }
}

extern "C" bool test_coro_await_suspend_bool_asm_label(void *a, void *b,
                                                       void *c) {
  try {
    return my_await_suspend_bool(a, b, c);
  } catch (...) {
    return false;
  }
}

extern "C" void *test_coro_await_suspend_handle_asm_label(void *a, void *b,
                                                          void *c) {
  try {
    return my_await_suspend_handle(a, b, c);
  } catch (...) {
    return nullptr;
  }
}

// CIR-LABEL: cir.func {{.*}} @test_coro_await_suspend_void_asm_label
// CIR: cir.call exception @__builtin_coro_await_suspend_void(%{{.*}}, %{{.*}}, %{{.*}})

// CIR-LABEL: cir.func {{.*}} @test_coro_await_suspend_bool_asm_label
// CIR: cir.call exception @__builtin_coro_await_suspend_bool(%{{.*}}, %{{.*}}, %{{.*}})

// CIR-LABEL: cir.func {{.*}} @test_coro_await_suspend_handle_asm_label
// CIR: cir.call exception @__builtin_coro_await_suspend_handle(%{{.*}}, %{{.*}}, %{{.*}})

// LLVM-NOT: @llvm.coro.await.suspend.void
// LLVM-NOT: @llvm.coro.await.suspend.bool
// LLVM-NOT: @llvm.coro.await.suspend.handle

// LLVM-LABEL: define {{.*}} @test_coro_await_suspend_void_asm_label(
// LLVM: invoke void @__builtin_coro_await_suspend_void(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})

// LLVM-LABEL: define {{.*}} @test_coro_await_suspend_bool_asm_label(
// LLVM: invoke zeroext i1 @__builtin_coro_await_suspend_bool(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})

// LLVM-LABEL: define {{.*}} @test_coro_await_suspend_handle_asm_label(
// LLVM: invoke ptr @__builtin_coro_await_suspend_handle(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})

// OGCG-NOT: @llvm.coro.await.suspend.void
// OGCG-NOT: @llvm.coro.await.suspend.bool
// OGCG-NOT: @llvm.coro.await.suspend.handle

// OGCG-LABEL: define {{.*}} @test_coro_await_suspend_void_asm_label(
// OGCG: invoke void @__builtin_coro_await_suspend_void(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})

// OGCG-LABEL: define {{.*}} @test_coro_await_suspend_bool_asm_label(
// OGCG: invoke zeroext i1 @__builtin_coro_await_suspend_bool(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})

// OGCG-LABEL: define {{.*}} @test_coro_await_suspend_handle_asm_label(
// OGCG: invoke ptr @__builtin_coro_await_suspend_handle(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
