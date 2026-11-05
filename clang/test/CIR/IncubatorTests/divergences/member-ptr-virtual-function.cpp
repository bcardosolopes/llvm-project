// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify pointer to virtual member function: the member function pointer
// ({i64, i64}) encoding vtable offset is decomposed into two i64 scalar
// arguments.
// Previously XFAIL due to member pointer not being decomposed per SysV ABI.
// Now correctly passes member function pointers as (i64, i64) and data
// member pointers as (i64).

struct Base {
    virtual int foo() { return 1; }
};

struct Derived : Base {
    int foo() override { return 2; }
};

int call_virtual_ptr(Base* b, int (Base::*ptr)()) {
    return (b->*ptr)();
}

int test() {
    Derived d;
    return call_virtual_ptr(&d, &Base::foo);
}

// CHECK: define {{.*}} i32 @_Z16call_virtual_ptrP4BaseMS_FivE(ptr noundef %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
