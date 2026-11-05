// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that guard variables for thread_local static locals are also
// thread-local (each thread gets its own guard, preventing data races).

struct S {
    int value;
    S(int v) : value(v) {}
};

int get_tls_static() {
    thread_local static S s(42);
    return s.value;
}

// The guard variable must have the same TLS model as the guarded variable.
// CIR: cir.global "private" internal tls_dyn dso_local @_ZGVZ14get_tls_staticvE1s
// CIR: cir.global "private" internal tls_dyn dso_local @_ZZ14get_tls_staticvE1s

// Guard access must use thread_local to get the per-thread address.
// CIR: cir.get_global thread_local @_ZGVZ14get_tls_staticvE1s

// Both must lower to LLVM thread_local globals.
// LLVM: @_ZGVZ14get_tls_staticvE1s = internal thread_local global
// LLVM: @_ZZ14get_tls_staticvE1s = internal thread_local global

// OGCG-DAG: @_ZZ14get_tls_staticvE1s = internal thread_local global
// OGCG-DAG: @_ZGVZ14get_tls_staticvE1s = internal thread_local global
