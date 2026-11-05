// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Template instantiations should be marked with comdat groups for proper
// ODR compliance when linking multiple TUs that instantiate the same template.

template<typename T>
struct Wrapper {
    T value;
    Wrapper(T v) : value(v) {}
    T get() const { return value; }
};

template<typename T>
T add(T a, T b) {
    return a + b;
}

int test_templates() {
    Wrapper<int> w(42);
    return w.get() + add(1, 2);
}

// CIR: define linkonce_odr {{.*}} @_Z3addIiET_S0_S0_({{.*}}) {{.*}} comdat
// CIR: define linkonce_odr {{.*}} @_ZN7WrapperIiEC1Ei({{.*}}) {{.*}} comdat

// OGCG: define linkonce_odr {{.*}} @_ZN7WrapperIiEC1Ei({{.*}}) {{.*}} comdat
// OGCG: define linkonce_odr {{.*}} @_Z3addIiET_S0_S0_({{.*}}) {{.*}} comdat
