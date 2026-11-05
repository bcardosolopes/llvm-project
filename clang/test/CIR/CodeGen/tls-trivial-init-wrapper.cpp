// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++17 %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll -check-prefix=LLVM
// RUN: FileCheck %s --input-file=%t.ll -check-prefix=LLVM-GUARD
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -std=c++17 %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll -check-prefix=OGCG
// RUN: FileCheck %s --input-file=%t.ogcg.ll -check-prefix=OGCG-GUARD

// Trivially-initialized TLS wrappers should NOT call __tls_init.
// Non-trivially-initialized TLS wrappers should call _ZTH<var> (alias).

struct S {
  int x;
  S(int v) : x(v) {}
  ~S() {}
};

thread_local S tls_obj(42);
thread_local int tls_int = 100;

int get_tls() { return tls_obj.x + tls_int; }

// _ZTH alias for non-trivially-initialized TLS variable.
// LLVM: @_ZTH7tls_obj = alias void (), ptr @__tls_init
// OGCG: @_ZTH7tls_obj = alias void (), ptr @__tls_init

// Non-trivially-initialized: calls _ZTH alias.
// LLVM: define weak_odr hidden noundef ptr @_ZTW7tls_obj()
// LLVM:   call void @_ZTH7tls_obj()
// OGCG: define weak_odr hidden noundef ptr @_ZTW7tls_obj()
// OGCG:   call void @_ZTH7tls_obj()

// Non-trivial TLS init uses the guard directly, not via llvm.threadlocal.address.
// LLVM-GUARD-LABEL: define internal void @__tls_init()
// LLVM-GUARD: load i8, ptr @__tls_guard, align 1
// LLVM-GUARD: store i8 1, ptr @__tls_guard, align 1
// LLVM-GUARD-NOT: @llvm.threadlocal.address.p0(ptr align 1 @__tls_guard)
// OGCG-GUARD-LABEL: define internal void @__tls_init()
// OGCG-GUARD: load i8, ptr @__tls_guard, align 1
// OGCG-GUARD: store i8 1, ptr @__tls_guard, align 1
// OGCG-GUARD-NOT: @llvm.threadlocal.address.p0(ptr align 1 @__tls_guard)

// Trivially-initialized: NO init call.
// LLVM: define weak_odr hidden noundef ptr @_ZTW7tls_int()
// LLVM-NOT: call void @__tls_init
// LLVM-NOT: call void @_ZTH
// LLVM:   call {{.*}}ptr @llvm.threadlocal.address.p0(ptr {{.*}}@tls_int)
// LLVM:   ret ptr

// OGCG: define weak_odr hidden noundef ptr @_ZTW7tls_int()
// OGCG-NOT: call void @__tls_init
// OGCG-NOT: call void @_ZTH
// OGCG:   call {{.*}}ptr @llvm.threadlocal.address.p0(ptr {{.*}}@tls_int)
// OGCG:   ret ptr
