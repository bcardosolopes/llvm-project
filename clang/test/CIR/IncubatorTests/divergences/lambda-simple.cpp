// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Simple lambda returning a constant. Previously XFAIL due to comdat
// divergence that no longer exists.

// CHECK: define internal noundef i32 @"_ZZ4testvENK3$_0clEv"(ptr noundef nonnull align 1 dereferenceable(1) %0)

int test() {
    auto f = []() { return 42; };
    return f();
}
