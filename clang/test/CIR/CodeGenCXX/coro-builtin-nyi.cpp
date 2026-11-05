// RUN: not %clang_cc1 -std=c++17 -fcoroutines -fclangir -emit-llvm %s -o /dev/null 2>&1 | FileCheck %s

// The original assertion crash was The remaining
// failure is a cir.store type mismatch: __builtin_coro_id returns a u32 token
// but the code tries to store it into a void*. OGCG also fails on this test
// ("storing unsized types is not allowed: store token ... ptr"). This is
// invalid usage — see coro-builtins-c.c for correct C coroutine builtin usage.
// CHECK: CIR-to-CIR transformation failed

void* test_coro() {
    void *hdl = __builtin_coro_id(0, 0, 0, 0);
    return hdl;
}
