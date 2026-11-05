// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify variadic function calls correctly distinguish required vs
// optional arguments for ABI classification.

extern "C" int printf(const char* fmt, ...);

// CHECK: define {{.*}} void @_Z4testv()
// CHECK:   call i32 (ptr, ...) @printf(ptr noundef {{.*}}, i32 noundef 42, double noundef 3.140000e+00)
// OGCG:    call i32 (ptr, ...) @printf(ptr {{.*}}, i32 {{.*}}42, double {{.*}}3.140000e+00)
void test() {
    printf("%d %f\n", 42, 3.14);
}
