// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Previously crashed with "lambda with destructed field" NYI.
// Lambda capture by value of types with non-trivial destructors now works.

struct Obj {
    int x;
    Obj(int v) : x(v) {}
    Obj(const Obj&);
    ~Obj();
};

int test_single_capture() {
    Obj o(42);
    auto f = [o]() { return o.x; };
    return f();
}

// CIR: cir.func {{.*}}@_Z19test_single_capturev()
// CIR:   cir.scope

// LLVM: define {{.*}}@_Z19test_single_capturev()

// Multiple captures with non-trivial destructors.
struct S {
    int val;
    S(int v) : val(v) {}
    S(const S&);
    ~S();
};

int test_multi_capture() {
    S a(1), b(2);
    auto f = [a, b]() { return a.val + b.val; };
    return f();
}

// LLVM: define {{.*}}@_Z18test_multi_capturev()
