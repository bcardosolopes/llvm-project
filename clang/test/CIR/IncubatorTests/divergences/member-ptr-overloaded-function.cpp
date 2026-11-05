// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify pointer to overloaded member function: the member function pointer
// ({i64, i64}) is decomposed into two i64 scalar arguments.
// Previously XFAIL due to member pointer not being decomposed per SysV ABI.
// Now correctly passes member function pointers as (i64, i64) and data
// member pointers as (i64).

struct S {
    int foo(int x) { return x; }
    int foo(double x) { return static_cast<int>(x); }
};

int call_overloaded(S* s, int (S::*ptr)(int)) {
    return (s->*ptr)(42);
}

int test() {
    S s;
    return call_overloaded(&s, static_cast<int (S::*)(int)>(&S::foo));
}

// CHECK: define {{.*}} i32 @_Z15call_overloadedP1SMS_FiiE(ptr noundef %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
