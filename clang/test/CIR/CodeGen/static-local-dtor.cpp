// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that function-local statics with non-trivial destructors
// register the destructor via __cxa_atexit.

struct S {
    int id;
    S(int i) : id(i) {}
    ~S() {}
};

// CHECK: @__dso_handle = external hidden global i8

S& get() {
    static S instance(42);
    return instance;
}

// CHECK: define {{.*}} @_Z3getv()
// CHECK:   call i32 @__cxa_guard_acquire(
// CHECK:   call void @_ZN1SC1Ei(
// CHECK:   call i32 @__cxa_atexit(ptr @_ZN1SD1Ev, ptr @_ZZ3getvE8instance, ptr @__dso_handle)
// CHECK:   call void @__cxa_guard_release(
