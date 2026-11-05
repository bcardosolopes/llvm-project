// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that complex function calls work correctly when EH cleanups are
// active (RAII destructors on the EH stack). Previously, the complex
// result from a try_call didn't dominate uses outside the try scope.

struct Guard {
    Guard();
    ~Guard();
};

_Complex double get_complex();

// CHECK-LABEL: define{{.*}} @_Z4testv
// CHECK: invoke {{.*}}get_complex
void test() {
    Guard g;
    _Complex double v = get_complex();
}

// CHECK-LABEL: define{{.*}} @_Z11test_returnv
_Complex double test_return() {
    Guard g;
    return get_complex();
}
