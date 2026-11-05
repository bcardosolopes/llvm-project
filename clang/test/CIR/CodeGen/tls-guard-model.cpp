// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm -ftls-model=local-exec %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm -ftls-model=local-exec %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// __tls_guard should use the -ftls-model setting, not hardcode
// general-dynamic.

int init();
thread_local int x = init();
int get_x() { return x; }

// CHECK: @__tls_guard = internal thread_local(localexec) global i8 0
// CHECK-LABEL: define internal void @__tls_init()
// CHECK: load i8, ptr @__tls_guard, align 1
// CHECK: store i8 1, ptr @__tls_guard, align 1
// CHECK-NOT: @llvm.threadlocal.address.p0(ptr align 1 @__tls_guard)
// OGCG: @__tls_guard = internal thread_local(localexec) global i8 0
// OGCG-LABEL: define internal void @__tls_init()
// OGCG: load i8, ptr @__tls_guard, align 1
// OGCG: store i8 1, ptr @__tls_guard, align 1
// OGCG-NOT: @llvm.threadlocal.address.p0(ptr align 1 @__tls_guard)
