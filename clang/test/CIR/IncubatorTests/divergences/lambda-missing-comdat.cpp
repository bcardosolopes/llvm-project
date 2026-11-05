// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Comprehensive lambda comdat test covering multiple lambda variants.
// Previously XFAIL due to comdat divergence that no longer exists.

// CHECK: define internal noundef i32 @"_ZZ4testvENK3$_0clEv"(ptr noundef nonnull align 1 dereferenceable(1) %0)
// CHECK: define internal noundef i32 @"_ZZ12test_capturevENK3$_0clEv"(ptr noundef nonnull align 4 dereferenceable(4) %0)
// CHECK: define internal noundef i32 @"_ZZ11test_paramsvENK3$_0clEii"(ptr noundef nonnull align 1 dereferenceable(1) %0, i32 noundef %1, i32 noundef %2)
// CHECK: define internal noundef i32 @"_ZZ12test_mutablevEN3$_0clEv"(ptr noundef nonnull align 4 dereferenceable(4) %0)
// CHECK: define internal i32 @"_ZZ18test_struct_returnvENK3$_0clEv"(ptr noundef nonnull align 1 dereferenceable(1) %0)

int test() {
    auto f = []() { return 42; };
    return f();
}

// Lambda with capture
int test_capture() {
    int x = 10;
    auto f = [x]() { return x * 2; };
    return f();
}

// Lambda taking parameters
int test_params() {
    auto f = [](int a, int b) { return a + b; };
    return f(10, 20);
}

// Mutable lambda
int test_mutable() {
    int x = 10;
    auto f = [x]() mutable { return ++x; };
    return f();
}

// Lambda returning struct
struct Result {
    int value;
};

int test_struct_return() {
    auto f = []() { return Result{42}; };
    return f().value;
}
