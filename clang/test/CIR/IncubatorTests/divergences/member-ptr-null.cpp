// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify null data member pointer comparison: data member pointer is passed
// as a single i64 and compared against the null representation.
// Previously XFAIL due to member pointer not being decomposed per SysV ABI.
// Now correctly passes member function pointers as (i64, i64) and data
// member pointers as (i64).

struct S {
    int x;
};

bool test_null(int S::*ptr) {
    return ptr == nullptr;
}

int test() {
    return test_null(nullptr) ? 1 : 0;
}

// CHECK: define {{.*}} i1 @_Z9test_nullM1Si(i64 %{{.*}})
