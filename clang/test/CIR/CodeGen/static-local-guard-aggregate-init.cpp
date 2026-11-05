// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Verify that thread-safe guarded static locals with aggregate initialization
// that pushes deferred deactivation cleanups (e.g. member destructors for
// exception safety) don't crash. The RunCleanupsScope around emitInitAndDtor()
// ensures deferred cleanups are deactivated before the guard-abort cleanup is
// popped.

struct Inner {
    int x;
    Inner(int v) : x(v) { if (v < 0) throw v; }
    ~Inner() {}
};

struct Aggregate {
    Inner a;
    Inner b;
};

Aggregate& get() {
    static Aggregate s{Inner(1), Inner(2)};
    return s;
}

// CIR: cir.func {{.*}} @_Z3getv
// CIR:   cir.if
// CIR:     cir.call @__cxa_guard_acquire({{.*}}) nothrow
// CIR:     cir.if
// CIR:       cir.call @__cxa_guard_release({{.*}}) nothrow

// LLVM: define {{.*}} @_Z3getv({{.*}}) {{.*}} personality ptr @__gxx_personality_v0
// LLVM:   call {{.*}} @__cxa_guard_acquire
// LLVM:   call void @__cxa_guard_release
