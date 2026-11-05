// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: FileCheck --check-prefix=CHECK-GUARD --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG
// RUN: FileCheck --check-prefix=OGCG-GUARD --input-file=%t.ogcg.ll %s

// TLS wrapper for thread_local reference must load the stored pointer,
// not just return the address of the TLS variable.

int &f();

thread_local int &r = f();

int &g() { return r; }

// The TLS initializer must also store through the per-thread TLS address,
// not directly to the TLS global symbol.
// CHECK-LABEL: define internal void @__cxx_global_var_init()
// CHECK: call align 8 ptr @llvm.threadlocal.address.p0(ptr align 8 @r)
// CHECK: call noundef nonnull align 4 dereferenceable(4) ptr @_Z1fv()
// CHECK: store ptr %{{.*}}, ptr %{{.*}}, align 8
// CHECK-NOT: store ptr %{{.*}}, ptr @r, align 8
// CHECK: ret void

// The TLS wrapper _ZTW1r must load through the TLS address for reference types.
// CHECK-LABEL: define weak_odr hidden noundef ptr @_ZTW1r()
// CHECK: call align 8 ptr @llvm.threadlocal.address.p0(ptr align 8 @r)
// CHECK: load ptr, ptr %{{.*}}, align 8
// CHECK: ret ptr %{{.*}}

// But the TLS init guard itself should be accessed directly.
// CHECK-GUARD-LABEL: define internal void @__tls_init()
// CHECK-GUARD: load i8, ptr @__tls_guard, align 1
// CHECK-GUARD: store i8 1, ptr @__tls_guard, align 1
// CHECK-GUARD: call void @__cxx_global_var_init()

// The caller of the TLS wrapper must use the wrapper result directly.
// CHECK: define dso_local noundef nonnull align 4 dereferenceable(4) ptr @_Z1gv()
// CHECK: %{{.*}} = call noundef ptr @_ZTW1r()
// CHECK-NEXT: ret ptr %{{.*}}

// OGCG-LABEL: define internal void @__cxx_global_var_init()
// OGCG: call noundef nonnull align 4 dereferenceable(4) ptr @_Z1fv()
// OGCG: call align 8 ptr @llvm.threadlocal.address.p0(ptr align 8 @r)
// OGCG: store ptr %{{.*}}, ptr %{{.*}}, align 8
// OGCG-NOT: store ptr %{{.*}}, ptr @r, align 8
// OGCG: ret void
// OGCG-LABEL: define dso_local noundef nonnull align 4 dereferenceable(4) ptr @_Z1gv()
// OGCG: %{{.*}} = call ptr @_ZTW1r()
// OGCG-NEXT: ret ptr %{{.*}}
// OGCG-LABEL: define weak_odr hidden noundef ptr @_ZTW1r()
// OGCG: call align 8 ptr @llvm.threadlocal.address.p0(ptr align 8 @r)
// OGCG: load ptr, ptr %{{.*}}, align 8
// OGCG: ret ptr %{{.*}}
// OGCG-GUARD-LABEL: define internal void @__tls_init()
// OGCG-GUARD: load i8, ptr @__tls_guard, align 1
// OGCG-GUARD: store i8 1, ptr @__tls_guard, align 1
// OGCG-GUARD: call void @__cxx_global_var_init()
