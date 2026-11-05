// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that transparent_union attribute is properly handled in function calls.
// The union argument should be coerced to the first member type.

typedef union {
    int i;
    float f;
} TU __attribute__((transparent_union));

void f0(TU tu);

void caller(void) {
    f0(42);
}

// CHECK-LABEL: define {{.*}} @caller
// CHECK: store i32 42, ptr %{{.*}}
// CHECK: call void @f0(i32 %{{.*}})
// CHECK: declare void @f0(i32)
