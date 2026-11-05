// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Test that ternary expressions returning lvalue references from temporaries
// with destructors compile correctly. The issue was that
// LexicalScope::cleanup() skipped emitting temporary destructors when a yield
// terminator was already present in a ternary branch, causing
// RunCleanupsScope's destructor to emit the cleanup AFTER the yield. This made
// the yield a non-terminator, so TernaryOp::build couldn't find the result
// type.

struct A {
    A() : i(0) {}
    ~A() {}
    int i;
    friend const A& operator<<(const A& a, int n) { return a; }
};

const A& identity(const A& a) { return a; }
void g(const A&) {}

// CIR-LABEL: cir.func {{.*}}@_Z5test1b
void test1(bool b) {
    // Ternary returning reference from temporary + operator overload.
    // CIR: cir.ternary
    g(b ? A() << 1 : A() << 2);
}

// CIR-LABEL: cir.func {{.*}}@_Z5test2b
void test2(bool b) {
    // Ternary returning reference from temporary + function call.
    // CIR: cir.ternary
    g(b ? identity(A()) : identity(A()));
}

// Verify that destructors appear AFTER ternary, guarded by cleanup flags.
// CIR-LABEL: cir.func {{.*}}@_Z5test3b
// CIR:   cir.ternary
// CIR:     cir.call @_ZN1AC
// CIR:     cir.call @_Z8identityRK1A
// CIR:     cir.yield %{{[0-9]+}} : !cir.ptr
// CIR:   }, false {
// CIR:     cir.call @_ZN1AC
// CIR:     cir.call @_Z8identityRK1A
// CIR:     cir.yield %{{[0-9]+}} : !cir.ptr
// CIR:   cir.if
// CIR:     cir.call @_ZN1AD
// CIR:   cir.if
// CIR:     cir.call @_ZN1AD
void test3(bool b) {
    const A& r = b ? identity(A()) : identity(A());
    g(r);
}

// LLVM: define {{.*}}@_Z5test1b
// LLVM: define {{.*}}@_Z5test2b
// LLVM: define {{.*}}@_Z5test3b
