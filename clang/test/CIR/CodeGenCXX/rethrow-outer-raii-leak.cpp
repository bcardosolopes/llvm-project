// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=CIR

// Rethrow from catch handler doesn't destroy function-scope RAII
// during stack unwinding. RAII objects outside the try-catch are leaked.
// This is now fixed.

static int alive = 0;
struct R {
    int id;
    R(int i) : id(i) { alive++; }
    ~R() { alive--; }
};

void fn() {
    R outer(100);
    try {
        throw 42;
    } catch (int e) {
        throw;
    }
}

// CIR: define {{.*}}@_Z2fnv()
// Verify that the rethrow path includes a landing pad that cleans up 'outer'
// CIR: invoke void @__cxa_rethrow()
// CIR-NEXT: to label %{{.*}} unwind label %[[LP:[0-9]+]]
// CIR: [[LP]]:
// CIR: call void @_ZN1RD1Ev
