// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that delete[] correctly subtracts the array cookie offset before
// calling operator delete[], and doesn't crash with "invalid pointer".
// Previously, CIR passed the array-start pointer instead of the
// allocation-start pointer to operator delete[].

struct S {
    int x;
    S() : x(1) {}
    ~S();
};

// CHECK-LABEL: define{{.*}} void @_Z4testv
// CHECK: call{{.*}} ptr @_Znam
// The delete[] path should subtract the cookie to get the allocation pointer.
// CHECK: getelementptr{{.*}} i8,{{.*}} i64 -8
// CHECK: call void @_ZdaPvm
void test() {
    S* p = new S[2];
    delete[] p;
}
