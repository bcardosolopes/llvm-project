// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcoroutines -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcoroutines -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcoroutines -emit-llvm -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test coroutine builtins called explicitly.
// These builtins are typically called by the <coroutine> standard library header.

void *test_coro_promise(void *handle) {
  return __builtin_coro_promise(handle, 16, 0);
}

// CIR-LABEL: @test_coro_promise
// CIR: cir.call @__builtin_coro_promise({{%.*}}, {{%.*}}, {{%.*}})

// LLVM-LABEL: @test_coro_promise
// LLVM: call ptr @llvm.coro.promise

// OGCG-LABEL: @test_coro_promise
// OGCG: call ptr @llvm.coro.promise

void test_coro_resume(void *handle) {
  __builtin_coro_resume(handle);
}

// CIR-LABEL: @test_coro_resume
// CIR: cir.call @__builtin_coro_resume({{%.*}})

// LLVM-LABEL: @test_coro_resume
// LLVM: call void @llvm.coro.resume

// OGCG-LABEL: @test_coro_resume
// OGCG: call void @llvm.coro.resume

void test_coro_destroy(void *handle) {
  __builtin_coro_destroy(handle);
}

// CIR-LABEL: @test_coro_destroy
// CIR: cir.call @__builtin_coro_destroy({{%.*}})

// LLVM-LABEL: @test_coro_destroy
// LLVM: call void @llvm.coro.destroy

// OGCG-LABEL: @test_coro_destroy
// OGCG: call void @llvm.coro.destroy

_Bool test_coro_done(void *handle) {
  return __builtin_coro_done(handle);
}

// CIR-LABEL: @test_coro_done
// CIR: cir.call @__builtin_coro_done({{%.*}})

// LLVM-LABEL: @test_coro_done
// LLVM: call i1 @llvm.coro.done

// OGCG-LABEL: @test_coro_done
// OGCG: call i1 @llvm.coro.done

void *test_coro_noop(void) {
  return __builtin_coro_noop();
}

// CIR-LABEL: @test_coro_noop
// CIR: cir.call @__builtin_coro_noop()

// LLVM-LABEL: @test_coro_noop
// LLVM: call ptr @llvm.coro.noop

// OGCG-LABEL: @test_coro_noop
// OGCG: call ptr @llvm.coro.noop
