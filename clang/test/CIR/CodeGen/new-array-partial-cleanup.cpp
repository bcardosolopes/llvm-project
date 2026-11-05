// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that new[] with init list correctly destroys already-constructed
// elements if a subsequent initializer throws (partial array cleanup).
// Previously, elements 0..i-1 were leaked when element i's init threw.

struct S {
    int id;
    S(int i);
    ~S();
};

// CHECK-LABEL: define{{.*}} ptr @_Z4testv
// The cleanup regions should call the destructor for partial cleanup.
// CHECK: landingpad
// CHECK: cleanup
// CHECK: call void @_ZN1SD1Ev
S* test() {
    return new S[4]{S(0), S(1), S(2), S(3)};
}
