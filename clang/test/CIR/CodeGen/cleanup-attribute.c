// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify __attribute__((cleanup)) calls the cleanup function with
// a pointer to the variable at scope exit.

void cleanup_int(int* p) { *p = 0; }
void cleanup_ptr(void** p) { *p = (void*)0; }

// CHECK: define {{.*}} i32 @test_cleanup()
// CHECK:   call void @cleanup_int(ptr noundef %{{.*}})
int test_cleanup() {
    int x __attribute__((cleanup(cleanup_int))) = 42;
    return x;
}

// CHECK: define {{.*}} void @test_cleanup_ptr()
// CHECK:   call void @cleanup_ptr(ptr noundef %{{.*}})
void test_cleanup_ptr() {
    void* p __attribute__((cleanup(cleanup_ptr))) = (void*)0;
}
