// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Move constructor comdat generation.

// CIR: $_ZN1SC1EOS_ = comdat any
// CIR: define linkonce_odr void @_ZN1SC1EOS_({{.*}}) unnamed_addr #{{[0-9]+}} comdat

// OGCG: $_ZN1SC1EOS_ = comdat any
// OGCG: define linkonce_odr void @_ZN1SC1EOS_({{.*}}) unnamed_addr #{{[0-9]+}} comdat

struct S {
    int* ptr;
    S(int val) : ptr(new int(val)) {}
    S(S&& other) : ptr(other.ptr) { other.ptr = nullptr; }
    ~S() { delete ptr; }
    int get() const { return ptr ? *ptr : 0; }
};

int test() {
    S s1(42);
    S s2(static_cast<S&&>(s1));
    return s2.get();
}
