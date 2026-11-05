// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify pointer to data member calling convention: data member pointers
// are passed as a single i64.
// Previously XFAIL due to member pointer not being decomposed per SysV ABI.
// Now correctly passes member function pointers as (i64, i64) and data
// member pointers as (i64).

struct S {
    int x, y;
};

int access_member(S* s, int S::*ptr) {
    return s->*ptr;
}

int test() {
    S s{1, 2};
    return access_member(&s, &S::x);
}

// CHECK: define {{.*}} i32 @_Z13access_memberP1SMS_i(ptr noundef %{{.*}}, i64 %{{.*}})
