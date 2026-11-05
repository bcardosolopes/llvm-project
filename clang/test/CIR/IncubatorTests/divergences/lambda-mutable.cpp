// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Mutable lambda (non-const operator()). Previously XFAIL due to comdat
// divergence that no longer exists.

// CHECK: define internal noundef i32 @"_ZZ4testvEN3$_0clEv"(ptr noundef nonnull align 4 dereferenceable(4) %0)

int test() {
    int x = 10;
    auto f = [x]() mutable { return ++x; };
    return f();
}
