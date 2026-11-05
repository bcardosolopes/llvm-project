// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-obj %s -o %t.o
// RUN: llvm-readelf --section-groups %t.o | FileCheck --input-file=- --check-prefix=ELF %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -emit-obj %s -o %t-ogcg.o
// RUN: llvm-readelf --section-groups %t-ogcg.o | FileCheck --input-file=- --check-prefix=ELF %s

// A polymorphic class (virtual destructor) explicitly instantiated produces
// weak_odr structors that take the COMDAT path. The base destructor D2 AND the
// deleting destructor D0 must share the D5 unified-structor comdat group, with
// D1 a weak alias to D2. This is the dominant weak_odr dtor shape in real code
// and exercises D0 placement, which a non-virtual dtor does not. The ELF
// section-group check (shared with OGCG) asserts the grouping survives to the
// object, where the original bug actually manifested.

template <typename T>
struct P {
  T x;
  P(T v) : x(v) {}
  virtual ~P() {}
};
template struct P<int>;

// IR: base structors keyed by the C5/D5 unified names.
// LLVM-DAG: $_ZN1PIiEC5Ei = comdat any
// LLVM-DAG: $_ZN1PIiED5Ev = comdat any
// LLVM-DAG: @_ZN1PIiED1Ev = weak_odr unnamed_addr alias {{.*}}, ptr @_ZN1PIiED2Ev
// LLVM-DAG: define weak_odr void @_ZN1PIiED2Ev(ptr {{.*}}) {{.*}}comdat($_ZN1PIiED5Ev)
// LLVM-DAG: define weak_odr void @_ZN1PIiED0Ev(ptr {{.*}}) {{.*}}comdat($_ZN1PIiED5Ev)

// Object: the D5 group owns BOTH the base (D2) and deleting (D0) dtor sections.
// ELF: COMDAT group section {{.*}} [_ZN1PIiED5Ev] contains
// ELF-DAG: .text._ZN1PIiED2Ev
// ELF-DAG: .text._ZN1PIiED0Ev
