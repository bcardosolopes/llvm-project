// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll --check-prefix=OGCG %s
// RUN: FileCheck --input-file=%t.cir --check-prefix=NOC1 %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=NOC1 %s
// RUN: FileCheck --input-file=%t-ogcg.ll --check-prefix=NOC1 %s

// A template class constructor has linkonce_odr linkage, which rules out an
// alias. The complete-object constructor is folded into the base one instead:
// CIRGen emits C1 as a delegating stub tagged with #cir.structor_dedup, and
// LoweringPrepare redirects every reference to C2 and erases the stub. OGCG
// reaches the same result through StructorCodegen::RAUW during CodeGen.

template <typename T>
struct Wrapper {
  T value;
  Wrapper(T v) : value(v) {}
};

void use() {
  Wrapper<int> w(42);
}

// Only the base constructor survives, and the call site targets it directly.
// NOC1-NOT: _ZN7WrapperIiEC1Ei

// CIR: cir.func {{.*}}@_Z3usev()
// CIR:   cir.call @_ZN7WrapperIiEC2Ei(
// CIR: cir.func {{.*}}linkonce_odr @_ZN7WrapperIiEC2Ei(
// CIR-NOT: cir.structor_dedup

// LLVM: define{{.*}} void @_Z3usev()
// LLVM:   call void @_ZN7WrapperIiEC2Ei(ptr
// LLVM: define linkonce_odr void @_ZN7WrapperIiEC2Ei(ptr {{.*}}, i32 {{.*}})

// OGCG: define{{.*}} void @_Z3usev()
// OGCG:   call void @_ZN7WrapperIiEC2Ei(ptr
// OGCG: define linkonce_odr void @_ZN7WrapperIiEC2Ei(ptr {{.*}}, i32 {{.*}})
