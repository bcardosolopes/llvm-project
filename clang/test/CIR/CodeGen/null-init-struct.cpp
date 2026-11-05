// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Zero-initialized structs use memset, matching OGCG behavior.

struct S {
    int x;
    int y;
};

// CHECK-NOT: @__const._Z4testv.s

// CHECK-LABEL: define {{.*}}@_Z4testv()
void test() {
    S s{};
    (void)s;
}

// CHECK: call void @llvm.memset
