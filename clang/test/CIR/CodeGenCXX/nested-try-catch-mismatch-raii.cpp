// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// RAII destructors must fire during exception propagation through
// non-matching catch handlers. When the inner catch(double) doesn't match
// the thrown int, the exception propagates to catch(int). RAII objects in
// both inner and outer try scopes must be destroyed.

int dtor_count = 0;
struct R {
    R() {}
    ~R() { dtor_count++; }
};

int test() {
    dtor_count = 0;
    try {
        R a;
        try {
            R b;
            throw 42;
        } catch (double) {}
    } catch (int) {}
    return dtor_count;
}

// Both CIR and OGCG must call the destructor twice in the function body:
// once for 'b' (inner scope) and once for 'a' (outer scope).

// CIR: define {{.*}} @_Z4testv()
// CIR: call void @_ZN1RD{{[12]}}Ev(
// CIR: call void @_ZN1RD{{[12]}}Ev(

// OGCG: define {{.*}} @_Z4testv()
// OGCG: call void @_ZN1RD{{[12]}}Ev(
// OGCG: call void @_ZN1RD{{[12]}}Ev(
