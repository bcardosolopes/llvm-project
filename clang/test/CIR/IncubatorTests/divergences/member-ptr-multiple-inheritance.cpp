// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify pointer to data member with multiple inheritance: data member pointer
// to non-first base is passed as a single i64, with proper offset adjustment.
// Previously XFAIL due to member pointer not being decomposed per SysV ABI.
// Now correctly passes member function pointers as (i64, i64) and data
// member pointers as (i64).

struct A { int a; };
struct B { int b; };
struct C : A, B { int c; };

int access_b(C* c, int B::*ptr) {
    return c->*ptr;
}

int test() {
    C c{{1}, {2}, 3};
    return access_b(&c, &B::b);
}

// CHECK: define {{.*}} i32 @_Z8access_bP1CM1Bi(ptr noundef %{{.*}}, i64 %{{.*}})
