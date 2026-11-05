// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify member pointer ABI calling convention: member function pointers
// ({i64, i64}) are decomposed into two separate i64 scalar arguments per
// the System V x86_64 ABI.
// Previously XFAIL due to member pointer not being decomposed per SysV ABI.
// Now correctly passes member function pointers as (i64, i64) and data
// member pointers as (i64).

struct S {
    int x;
    int foo(int y) { return x + y; }
};

int call_member_ptr(S* s, int (S::*ptr)(int), int val) {
    return (s->*ptr)(val);
}

int test() {
    S s;
    s.x = 42;
    return call_member_ptr(&s, &S::foo, 10);
}

// CHECK: define {{.*}} i32 @_Z15call_member_ptrP1SMS_FiiEi(ptr noundef %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i32 noundef %{{.*}})
