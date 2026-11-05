// RUN: %clang_cc1 -std=c++11 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++11 -triple x86_64-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// Bug 2/3: Template TLS init aliases should point to the individual
// init function, not to __tls_init. Ordered (non-template) TLS inits go
// into __tls_init; unordered (template) TLS inits have individual functions.

int f();

// Ordered TLS variable: init goes into __tls_init.
thread_local int a = f();

// Unordered (template) TLS variable: has its own guarded init function.
template<typename T> struct V { static thread_local int m; };
template<typename T> thread_local int V<T>::m = f();
void *e = &V<int>::m;

// Ordered TLS alias should point to __tls_init.
// LLVM-DAG: @_ZTH1a = alias void (), ptr @__tls_init
// OGCG-DAG: @_ZTH1a = alias void (), ptr @__tls_init

// Unordered (template) TLS alias should point to individual init function,
// NOT to __tls_init.
// LLVM-DAG: @_ZTHN1VIiE1mE = linkonce_odr alias void (), ptr @__cxx_global_var_init
// OGCG-DAG: @_ZTHN1VIiE1mE = linkonce_odr alias void (), ptr @__cxx_global_var_init

// __tls_init should only call the ordered init (for 'a'), not the template init.
// LLVM-LABEL: define internal void @__tls_init
// LLVM: call void @__cxx_global_var_init()
// LLVM-NOT: call void @__cxx_global_var_init.
// LLVM: ret void

// OGCG-LABEL: define internal void @__tls_init
// OGCG: call void @__cxx_global_var_init()
// OGCG-NOT: call void @__cxx_global_var_init.
// OGCG: ret void
