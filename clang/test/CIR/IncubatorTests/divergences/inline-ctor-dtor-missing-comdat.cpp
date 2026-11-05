// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that CIR emits comdat for inline constructors and destructors,
// matching CodeGen behavior per the Itanium C++ ABI.

// CIR: $_ZN1SC1Ev = comdat any
// CIR: $_ZN1SD1Ev = comdat any
// CIR: $_ZN10DelegatingC1Ei = comdat any
// CIR: define linkonce_odr {{.*}} @_ZN1SC1Ev{{.*}} comdat
// CIR: define linkonce_odr {{.*}} @_ZN1SD1Ev{{.*}} comdat
// CIR: define linkonce_odr {{.*}} @_ZN10DelegatingC1Ei{{.*}} comdat

// OGCG: $_ZN1SC1Ev = comdat any
// OGCG: $_ZN1SD1Ev = comdat any
// OGCG: $_ZN10DelegatingC1Ei = comdat any
// OGCG: define linkonce_odr {{.*}} @_ZN1SC1Ev{{.*}} comdat
// OGCG: define linkonce_odr {{.*}} @_ZN1SD1Ev{{.*}} comdat
// OGCG: define linkonce_odr {{.*}} @_ZN10DelegatingC1Ei{{.*}} comdat

struct S {
    int x;

    // Inline constructor
    S() : x(42) {}

    // Inline destructor
    ~S() {}
};

int test_ctor_dtor() {
    S s;
    return s.x;
}

// Test with parameterized constructor
struct WithParams {
    int a, b;

    WithParams(int x, int y) : a(x), b(y) {}
    ~WithParams() {}
};

int test_params() {
    WithParams w(10, 20);
    return w.a + w.b;
}

// Test with delegating constructor
struct Delegating {
    int x, y;

    Delegating(int a) : Delegating(a, a * 2) {}
    Delegating(int a, int b) : x(a), y(b) {}
    ~Delegating() {}
};

int test_delegating() {
    Delegating d(5);
    return d.x + d.y;
}

// Test with copy constructor
struct WithCopy {
    int val;

    WithCopy(int v) : val(v) {}
    WithCopy(const WithCopy& other) : val(other.val * 2) {}
    ~WithCopy() {}
};

int test_copy() {
    WithCopy w1(10);
    WithCopy w2(w1);
    return w2.val;
}

// Test with move constructor
struct WithMove {
    int* ptr;

    WithMove(int v) : ptr(new int(v)) {}
    WithMove(WithMove&& other) : ptr(other.ptr) { other.ptr = nullptr; }
    ~WithMove() { delete ptr; }

    int get() const { return ptr ? *ptr : 0; }
};

int test_move() {
    WithMove w1(42);
    WithMove w2(static_cast<WithMove&&>(w1));
    return w2.get();
}
