// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify member pointer to base class data member accessed via derived pointer:
// data member pointer is passed as a single i64.
// Previously XFAIL due to member pointer not being decomposed per SysV ABI.
// Now correctly passes member function pointers as (i64, i64) and data
// member pointers as (i64).

struct Base {
    int x;
};

struct Derived : Base {
    int y;
};

int access_base_member(Derived* d, int Base::*ptr) {
    return d->*ptr;
}

int test() {
    Derived d{{42}, 100};
    return access_base_member(&d, &Base::x);
}

// CHECK: define {{.*}} i32 @_Z18access_base_memberP7DerivedM4Basei(ptr noundef %{{.*}}, i64 %{{.*}})
