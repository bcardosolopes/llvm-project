// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s < %t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s < %t.ll

// Variadic thunks use musttail to forward varargs without cloning.

struct A {
    virtual int foo() { return 0; }
    virtual ~A() {}
};

struct B {
    virtual void log(const char *fmt, ...) {}
    virtual ~B() {}
};

struct C : A, B {
    void log(const char *fmt, ...) override {}
};

int main() {
    C c;
    B *bp = &c;
    bp->log("test %d", 42);
    return 0;
}

// CIR: cir.func {{.*}} @_ZThn8_N1C3logEPKcz(
// CIR:   cir.call must_tail @_ZN1C3logEPKcz(
// CIR:   cir.return

// LLVM: define {{.*}} @_ZThn8_N1C3logEPKcz(
// LLVM:   musttail call void (ptr, ptr, ...) @_ZN1C3logEPKcz(
// LLVM:   ret void
