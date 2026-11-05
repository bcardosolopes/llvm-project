// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify pointer to const member function: the member function pointer
// ({i64, i64}) is decomposed into two i64 scalar arguments.
// Previously XFAIL due to member pointer not being decomposed per SysV ABI.
// Now correctly passes member function pointers as (i64, i64) and data
// member pointers as (i64).

struct S {
    int get() const { return 42; }
};

int call_const_method(const S* s, int (S::*ptr)() const) {
    return (s->*ptr)();
}

int test() {
    S s;
    return call_const_method(&s, &S::get);
}

// CHECK: define {{.*}} i32 @_Z17call_const_methodPK1SMS_KFivE(ptr noundef %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
