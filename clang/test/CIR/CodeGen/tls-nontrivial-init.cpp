// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Verify that thread_local variables with non-trivial constructors use
// lazy initialization via __tls_init with a __tls_guard, not eager
// initialization via _GLOBAL__sub_I_*.

struct TLSObject {
  int value;
  TLSObject() : value(42) {}
  ~TLSObject();
};

thread_local TLSObject tls_obj;

int get_value() { return tls_obj.value; }

// CIR-DAG: cir.global "private"  internal tls_dyn  @__tls_guard = #cir.int<0> : !u8i

// The _ZTH alias points to __tls_init (per Itanium ABI).
// CIR-DAG: cir.func private @_ZTH7tls_obj() alias(@__tls_init)

// The wrapper calls _ZTH<var> (the alias) before returning the TLS address.
// CIR: cir.func no_inline comdat weak_odr private hidden @_ZTW7tls_obj
// CIR:   cir.call @_ZTH7tls_obj()
// CIR:   cir.get_global thread_local @tls_obj

// __tls_init has the guard check pattern.
// CIR: cir.func no_inline internal private @__tls_init()
// CIR:   cir.get_global @__tls_guard
// CIR:   cir.cmp(eq,
// CIR:   cir.brcond
// CIR:   cir.call @__cxx_global_var_init

// Ensure TLS init is NOT in the eager global init function.
// CIR-NOT: cir.func{{.*}}@_GLOBAL__sub_I_

// LLVM: @__tls_guard = internal thread_local global i8 0
// LLVM: @_ZTH7tls_obj = alias void (), ptr @__tls_init
// LLVM: define weak_odr hidden {{.*}} ptr @_ZTW7tls_obj()
// LLVM:   call void @_ZTH7tls_obj()
// LLVM: define internal void @__tls_init()
// LLVM:   load i8, ptr
// LLVM:   store i8 1, ptr

// OGCG: @__tls_guard = internal thread_local global i8 0
// OGCG: @_ZTH7tls_obj = alias void (), ptr @__tls_init
// OGCG: define weak_odr hidden {{.*}} ptr @_ZTW7tls_obj()
// OGCG:   call void @_ZTH7tls_obj()
// OGCG: define internal void @__tls_init()
