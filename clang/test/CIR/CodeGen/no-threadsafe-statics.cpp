// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++17 -fno-threadsafe-statics %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Verify that -fno-threadsafe-statics uses simple byte-store guards
// instead of __cxa_guard_acquire/__cxa_guard_release.

struct S {
    int x;
    S();
    ~S();
};

int get() {
    static S s;
    return s.x;
}

// For local variables, guard byte is set AFTER init (allows retry on exception):
// LLVM-LABEL: @_Z3getv()
// LLVM:   load i8, ptr @_ZGVZ3getvE1s
// LLVM:   call void @_ZN1SC1Ev(
// LLVM:   call {{.*}}@__cxa_atexit(
// LLVM:   store i8 1, ptr @_ZGVZ3getvE1s

// No __cxa_guard_acquire or __cxa_guard_release should be used:
// LLVM-NOT: @__cxa_guard_acquire
// LLVM-NOT: @__cxa_guard_release
