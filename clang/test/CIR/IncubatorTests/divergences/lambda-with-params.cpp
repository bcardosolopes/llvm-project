// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Lambda taking two int parameters. Previously XFAIL due to comdat divergence
// that no longer exists.

// CHECK: define internal noundef i32 @"_ZZ4testvENK3$_0clEii"(ptr noundef nonnull align 1 dereferenceable(1) %0, i32 noundef %1, i32 noundef %2)

int test() {
    auto f = [](int a, int b) { return a + b; };
    return f(10, 20);
}
