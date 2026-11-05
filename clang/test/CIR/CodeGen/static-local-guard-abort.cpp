// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that __cxa_guard_abort is emitted on the exceptional path when a
// guarded static local variable's constructor throws. Per the Itanium C++ ABI,
// the guard must be released on exception so future calls can retry.

struct S {
    int val;
    S(int v) : val(v) { if (v < 0) throw v; }
};

S& get(int v) {
    static S s(v);
    return s;
}

// CIR: cir.func private @__cxa_guard_abort
// CIR: cir.func {{.*}} @_Z3geti
// CIR:   cir.if
// CIR:     cir.call @__cxa_guard_acquire({{.*}}) nothrow
// CIR:     cir.if
// CIR:       cir.call @__cxa_guard_abort({{.*}}) nothrow
// CIR:       cir.call @__cxa_guard_release({{.*}}) nothrow

// The guarded initialization sits in a cir.cleanup.scope, whose cleanup region
// lowers right after the body -- so the guard_abort unwind path is laid out
// before the normal-path guard_release. Block order differs from OGCG, the
// edges do not.
// LLVM: define {{.*}} @_Z3geti({{.*}}) {{.*}} personality ptr @__gxx_personality_v0
// LLVM:   call {{.*}} @__cxa_guard_acquire
// LLVM:   invoke void @_ZN1SC1Ei
// LLVM:   landingpad { ptr, i32 }
// LLVM-NEXT: cleanup
// LLVM:   call void @__cxa_guard_abort
// LLVM:   resume { ptr, i32 }
// LLVM:   call void @__cxa_guard_release

// OGCG: define {{.*}} @_Z3geti({{.*}}) {{.*}} personality ptr @__gxx_personality_v0
// OGCG:   call {{.*}} @__cxa_guard_acquire
// OGCG:   invoke void @_ZN1SC1Ei
// OGCG:   call {{.*}} @__cxa_guard_release
// OGCG:   landingpad { ptr, i32 }
// OGCG-NEXT: cleanup
// OGCG:   call void @__cxa_guard_abort
// OGCG:   resume { ptr, i32 }
