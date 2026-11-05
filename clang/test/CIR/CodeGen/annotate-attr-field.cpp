// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that fields with __attribute__((annotate(...))) emit llvm.ptr.annotation
// calls when accessed.

struct S {
    int x __attribute__((annotate("special")));
};

int test(S s) {
    return s.x;
}

// CHECK-LABEL: define {{.*}} @_Z4test1S
// CHECK: getelementptr inbounds nuw %struct.S, ptr %{{.*}}, i32 0, i32 0
// CHECK: call ptr @llvm.ptr.annotation.p0.p0(ptr %{{.*}}, ptr @{{.*}}, ptr @{{.*}}, i32 8, ptr null)
// CHECK: ret i32
