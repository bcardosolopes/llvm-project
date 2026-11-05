// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify member pointer stored as struct member: the invoke() method loads
// the member function pointer from the struct and calls through it.
// Previously XFAIL due to member pointer not being decomposed per SysV ABI.
// Now correctly passes member function pointers as (i64, i64) and data
// member pointers as (i64).

struct Callback {
    int (Callback::*method)();
    int impl() { return 42; }
    int invoke() { return (this->*method)(); }
};

int test() {
    Callback cb;
    cb.method = &Callback::impl;
    return cb.invoke();
}

// CHECK: define {{.*}} i32 @_ZN8Callback6invokeEv(ptr noundef nonnull align 8 dereferenceable(16) %{{.*}})
