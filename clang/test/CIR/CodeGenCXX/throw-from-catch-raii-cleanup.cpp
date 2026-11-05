// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// When a NEW exception is thrown from inside a catch handler, RAII
// objects in the enclosing try scope must be destroyed during unwinding.
// The throw from the catch handler propagates to the outer catch, and the
// cleanup chain must include destructors for objects in the outer try scope.

int alive = 0;
struct R {
    R() { alive++; }
    ~R() { alive--; }
};

int test() {
    alive = 0;
    try {
        R r;  // alive = 1
        try { throw 1; } catch (int) { throw 2; }  // r must be destroyed
    } catch (int e) {
        return alive;
    }
    return -1;
}

// Both CIR and OGCG must call the destructor in the cleanup path.
// The destructor call for 'r' appears in the landing pad / cleanup region
// when throw 2 triggers unwinding through the outer try scope.

// CIR: define {{.*}} @_Z4testv()
// CIR: call void @_ZN1RD{{[12]}}Ev(

// OGCG: define {{.*}} @_Z4testv()
// OGCG: call void @_ZN1RD{{[12]}}Ev(
