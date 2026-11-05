// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++17 %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Verify that function-local thread_local destructors are registered with
// __cxa_thread_atexit (not __cxa_atexit), ensuring LIFO ordering with
// global thread_local destructors.

struct S {
    int x;
    S() : x(0) {}
    ~S() {}
};

thread_local S global_tls;

S& get_local_tls() {
    thread_local S local_tls;
    return local_tls;
}

// Global TLS destructor uses __cxa_thread_atexit:
// LLVM: define internal void @__cxx_global_var_init()
// LLVM:   call {{.*}}@__cxa_thread_atexit(ptr @_ZN1SD1Ev, ptr @global_tls, ptr @__dso_handle)
// LLVM-NOT: @llvm.threadlocal.address.p0(ptr align 4 @global_tls)

// Function-local TLS destructor also uses __cxa_thread_atexit:
// LLVM: define {{.*}}@_Z13get_local_tlsv()
// LLVM:   call {{.*}}@__cxa_thread_atexit(ptr @_ZN1SD1Ev, ptr @_ZZ13get_local_tlsvE9local_tls, ptr @__dso_handle)
// LLVM-NOT: @llvm.threadlocal.address.p0(ptr align 4 @_ZZ13get_local_tlsvE9local_tls)

// No __cxa_atexit should be used for TLS variables:
// LLVM-NOT: @__cxa_atexit
