// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// When a rethrow (throw;) happens inside a catch handler, RAII objects in the
// enclosing try scope must be destroyed during unwinding. The rethrow propagates
// the exception to the outer catch, and the cleanup chain must include
// destructors for objects in the outer try scope.

int alive = 0;
struct R {
    R() { alive++; }
    ~R() { alive--; }
};

int test() {
    alive = 0;
    try {
        R r;  // alive = 1
        try { throw 1; } catch (int) { throw; }  // rethrow — r must be destroyed
    } catch (int) {
        return alive;
    }
    return -1;
}

// Both CIR and OGCG must invoke __cxa_rethrow (not a plain call) so that
// the landing pad can run the RAII cleanup for 'r' during unwinding.

// CIR: define {{.*}} @_Z4testv()
// CIR: invoke void @__cxa_rethrow()
// CIR: call void @_ZN1RD{{[12]}}Ev(

// OGCG: define {{.*}} @_Z4testv()
// OGCG: invoke void @__cxa_rethrow()
// OGCG: call void @_ZN1RD{{[12]}}Ev(
