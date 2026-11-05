// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that guard variables for static locals absorb linkage from
// the guarded variable, and that the guard byte load uses atomic acquire
// ordering for thread-safe statics.

int init();

// Regular function: internal linkage for both variable and guard.
int regular() {
    static int x = init();
    return x;
}

// Inline function: linkonce_odr linkage for both variable and guard.
inline int inlined() {
    static int y = init();
    return y;
}

int caller() {
    return regular() + inlined();
}

// Guard linkage must match the guarded variable's linkage.

// CIR: cir.global "private" internal dso_local @_ZGVZ7regularvE1x = #cir.int<0> : !u64i
// CIR: cir.global linkonce_odr comdat @_ZGVZ7inlinedvE1y = #cir.int<0> : !u64i

// CIR: cir.func {{.*}} @_Z7regularv
// CIR:   cir.load {{.*}} atomic(acquire) {{.*}} : !cir.ptr<!u8i>, !u8i

// CIR: cir.func {{.*}} @_Z7inlinedv
// CIR:   cir.load {{.*}} atomic(acquire) {{.*}} : !cir.ptr<!u8i>, !u8i

// LLVM: @_ZGVZ7regularvE1x = internal global i64 0
// LLVM: @_ZGVZ7inlinedvE1y = linkonce_odr global i64 0, comdat

// LLVM: define {{.*}} @_Z7regularv
// LLVM:   load atomic i8, ptr @_ZGVZ7regularvE1x acquire, align 8

// LLVM: define {{.*}} @_Z7inlinedv
// LLVM:   load atomic i8, ptr @_ZGVZ7inlinedvE1y acquire, align 8

// OGCG: @_ZGVZ7regularvE1x = internal global i64 0
// OGCG: @_ZGVZ7inlinedvE1y = linkonce_odr global i64 0, comdat

// OGCG: define {{.*}} @_Z7regularv
// OGCG:   load atomic i8, ptr @_ZGVZ7regularvE1x acquire

// OGCG: define {{.*}} @_Z7inlinedv
// OGCG:   load atomic i8, ptr @_ZGVZ7inlinedvE1y acquire
