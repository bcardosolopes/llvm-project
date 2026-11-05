// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify member pointer comparison: two data member pointers are each passed
// as a single i64.
// Previously XFAIL due to member pointer not being decomposed per SysV ABI.
// Now correctly passes member function pointers as (i64, i64) and data
// member pointers as (i64).

struct S {
    int x, y;
};

bool compare_member_ptrs(int S::*p1, int S::*p2) {
    return p1 == p2;
}

int test() {
    return compare_member_ptrs(&S::x, &S::x) ? 1 : 0;
}

// CHECK: define {{.*}} i1 @_Z19compare_member_ptrsM1SiS0_(i64 %{{.*}}, i64 %{{.*}})
