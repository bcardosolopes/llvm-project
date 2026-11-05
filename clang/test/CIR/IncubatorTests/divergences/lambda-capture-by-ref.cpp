// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Lambda with capture by reference. Previously XFAIL due to comdat divergence
// that no longer exists.

// CHECK: define internal noundef i32 @"_ZZ4testvENK3$_0clEv"(ptr noundef nonnull align 8 dereferenceable(8) %0)

int test() {
    int x = 10;
    auto f = [&x]() { x *= 2; return x; };
    return f();
}
