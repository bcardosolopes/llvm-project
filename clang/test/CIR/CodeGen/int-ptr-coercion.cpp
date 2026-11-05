// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Regression test: Int↔Ptr coercion in
// castReturnValue and coerceIntOrPtrToIntOrPtr.

struct S {
    void *p;
};

// A struct with a single pointer field gets coerced at the ABI level.
// The function should compile and return a valid value, not crash.
// CHECK-LABEL: define {{.*}}@_Z4testv()
S test() {
    S s;
    s.p = (void*)0x1234;
    return s;
}

// CHECK: ret ptr
