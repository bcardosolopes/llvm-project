// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// TLS variables with C++ destructors must use __cxa_thread_atexit
// (per-thread), not __cxa_atexit (per-process).

struct S { ~S(); };
thread_local S tls_var;
void use() { (void)tls_var; }

// CIR-LABEL: define internal void @__cxx_global_var_init()
// CIR: call {{.*}} @__cxa_thread_atexit(ptr @_ZN1SD1Ev, ptr @tls_var, ptr @__dso_handle)
// CIR-NOT: call {{.*}} @__cxa_atexit(
// CIR-NOT: @llvm.threadlocal.address.p0(ptr align 1 @tls_var)
// CIR: ret void
// OGCG-LABEL: define internal void @__cxx_global_var_init()
// OGCG: call {{.*}} @__cxa_thread_atexit(ptr @_ZN1SD1Ev, ptr @tls_var, ptr @__dso_handle)
// OGCG-NOT: call {{.*}} @__cxa_atexit(
// OGCG-NOT: @llvm.threadlocal.address.p0(ptr align 1 @tls_var)
// OGCG: ret void
