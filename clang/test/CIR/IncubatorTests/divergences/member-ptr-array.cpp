// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify array of data member pointers: each element is a simple i64 offset,
// and array indexing/access works correctly.
// Previously XFAIL due to member pointer not being decomposed per SysV ABI.
// Now correctly passes member function pointers as (i64, i64) and data
// member pointers as (i64).

struct S {
    int a, b, c;
};

int access_by_index(S* s, int index) {
    int S::*ptrs[] = {&S::a, &S::b, &S::c};
    return s->*ptrs[index];
}

int test() {
    S s{10, 20, 30};
    return access_by_index(&s, 1);
}

// CHECK: define {{.*}} i32 @_Z15access_by_indexP1Si(ptr noundef %{{.*}}, i32 noundef %{{.*}})
