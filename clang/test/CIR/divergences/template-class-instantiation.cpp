// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Template class instantiation comdat generation.

// CIR-DAG: $_ZN9ContainerIiEC1Ei = comdat any
// CIR-DAG: $_ZNK9ContainerIiE3getEv = comdat any
// CIR-DAG: define linkonce_odr void @_ZN9ContainerIiEC1Ei({{.*}}) unnamed_addr #{{[0-9]+}} comdat
// CIR-DAG: define linkonce_odr {{.*}} @_ZNK9ContainerIiE3getEv({{.*}}) #{{[0-9]+}} comdat

// OGCG: $_ZN9ContainerIiEC1Ei = comdat any
// OGCG: $_ZNK9ContainerIiE3getEv = comdat any
// OGCG: define linkonce_odr void @_ZN9ContainerIiEC1Ei({{.*}}) unnamed_addr #{{[0-9]+}} comdat
// OGCG: define linkonce_odr {{.*}} @_ZNK9ContainerIiE3getEv({{.*}}) #{{[0-9]+}} comdat

template<typename T>
struct Container {
    T value;
    Container(T v) : value(v) {}
    T get() const { return value; }
};

int test() {
    Container<int> c(42);
    return c.get();
}
