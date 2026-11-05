// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll --check-prefix=OGCG %s

// Weak_odr structors (here via explicit template instantiation) take the COMDAT
// path under -mconstructor-aliases on ELF. The Itanium ABI requires the base
// structor (C2/D2) to live in a COMDAT group keyed by the special unified
// structor name (C5/D5), NOT by the base structor's own symbol name. This keeps
// the C1/D1 weak alias and the C2/D2 body together as one linker unit; naming
// the group after C2/D2 can drop the alias from a non-prevailing copy.

template <typename T>
struct W {
  T v;
  W(T x) : v(x) {}
  ~W() {}
};

template struct W<int>;

// The base structor bodies must be in C5/D5-named COMDAT groups.
// LLVM-DAG: $_ZN1WIiEC5Ei = comdat any
// LLVM-DAG: $_ZN1WIiED5Ev = comdat any

// The complete structors are weak_odr aliases to the base structors.
// LLVM-DAG: @_ZN1WIiEC1Ei = weak_odr unnamed_addr alias {{.*}}, ptr @_ZN1WIiEC2Ei
// LLVM-DAG: @_ZN1WIiED1Ev = weak_odr unnamed_addr alias {{.*}}, ptr @_ZN1WIiED2Ev

// The base structor bodies reference the C5/D5 comdat group, not their own name.
// LLVM-DAG: define weak_odr void @_ZN1WIiEC2Ei(ptr {{.*}}, i32 {{.*}}) {{.*}}comdat($_ZN1WIiEC5Ei)
// LLVM-DAG: define weak_odr void @_ZN1WIiED2Ev(ptr {{.*}}) {{.*}}comdat($_ZN1WIiED5Ev)

// CIR output must match OGCG's C5/D5 grouping.
// OGCG-DAG: $_ZN1WIiEC5Ei = comdat any
// OGCG-DAG: $_ZN1WIiED5Ev = comdat any
// OGCG-DAG: define weak_odr void @_ZN1WIiEC2Ei(ptr {{.*}}, i32 {{.*}}) {{.*}}comdat($_ZN1WIiEC5Ei)
// OGCG-DAG: define weak_odr void @_ZN1WIiED2Ev(ptr {{.*}}) {{.*}}comdat($_ZN1WIiED5Ev)
