// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Verify that thread_local variables with non-trivial destructors use
// __cxa_thread_atexit for per-thread cleanup registration.

class Foo {
public:
  ~Foo();
};

thread_local Foo tls_foo;

// CIR: cir.func private @__cxa_thread_atexit
// CIR: cir.global{{.*}}tls_dyn{{.*}}@tls_foo
// CIR: cir.call @__cxa_thread_atexit

// LLVM: @tls_foo = thread_local global
// LLVM: declare {{.*}}i32 @__cxa_thread_atexit
// LLVM-LABEL: define internal void @__cxx_global_var_init()
// LLVM: call i32 @__cxa_thread_atexit(ptr @_ZN3FooD1Ev, ptr @tls_foo, ptr @__dso_handle)
// LLVM-NOT: @llvm.threadlocal.address.p0(ptr align 1 @tls_foo)
// LLVM: ret void
// LLVM-LABEL: define internal void @__tls_init()
// LLVM: load i8, ptr @__tls_guard, align 1
// LLVM: store i8 1, ptr @__tls_guard, align 1
// LLVM-NOT: @llvm.threadlocal.address.p0(ptr align 1 @__tls_guard)

// OGCG: @tls_foo = thread_local global
// OGCG-LABEL: define internal void @__cxx_global_var_init()
// OGCG: call {{.*}} @__cxa_thread_atexit(ptr @_ZN3FooD1Ev, ptr @tls_foo, ptr @__dso_handle)
// OGCG-NOT: @llvm.threadlocal.address.p0(ptr align 1 @tls_foo)
// OGCG: ret void
// OGCG: declare i32 @__cxa_thread_atexit
// OGCG-LABEL: define internal void @__tls_init()
// OGCG: load i8, ptr @__tls_guard, align 1
// OGCG: store i8 1, ptr @__tls_guard, align 1
// OGCG-NOT: @llvm.threadlocal.address.p0(ptr align 1 @__tls_guard)
