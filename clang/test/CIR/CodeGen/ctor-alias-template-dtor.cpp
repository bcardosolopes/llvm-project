// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll --check-prefix=OGCG %s
// RUN: FileCheck --input-file=%t.cir --check-prefix=NOCOMPLETE %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=NOCOMPLETE %s
// RUN: FileCheck --input-file=%t-ogcg.ll --check-prefix=NOCOMPLETE %s

// Both the constructor and the destructor of a linkonce_odr template class are
// folded into their base variants, matching OGCG's StructorCodegen::RAUW.

template <typename T>
struct Container {
  T data;
  Container(T d) : data(d) {}
  ~Container() {}
};

void test() {
  Container<int> c(10);
}

// Neither complete-object variant reaches the output.
// NOCOMPLETE-NOT: _ZN9ContainerIiEC1Ei
// NOCOMPLETE-NOT: _ZN9ContainerIiED1Ev

// CIR: cir.func {{.*}}@_Z4testv()
// CIR:   cir.call @_ZN9ContainerIiEC2Ei(
// CIR:   cir.call @_ZN9ContainerIiED2Ev(
// CIR-DAG: cir.func {{.*}}linkonce_odr @_ZN9ContainerIiEC2Ei(
// CIR-DAG: cir.func {{.*}}linkonce_odr @_ZN9ContainerIiED2Ev(

// LLVM: define{{.*}} void @_Z4testv()
// LLVM:   call void @_ZN9ContainerIiEC2Ei(ptr
// LLVM:   call void @_ZN9ContainerIiED2Ev(ptr
// LLVM-DAG: define linkonce_odr void @_ZN9ContainerIiEC2Ei(ptr
// LLVM-DAG: define linkonce_odr void @_ZN9ContainerIiED2Ev(ptr

// OGCG: define{{.*}} void @_Z4testv()
// OGCG:   call void @_ZN9ContainerIiEC2Ei(ptr
// OGCG:   call void @_ZN9ContainerIiED2Ev(ptr
// OGCG-DAG: define linkonce_odr void @_ZN9ContainerIiEC2Ei(ptr
// OGCG-DAG: define linkonce_odr void @_ZN9ContainerIiED2Ev(ptr
