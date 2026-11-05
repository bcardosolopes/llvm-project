// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll --check-prefix=OGCG %s

// Negative test for the C5/D5 comdat naming. When a class has virtual bases the
// complete (C1) and base (C2) constructors are NOT equivalent, so getCIRGenToUse
// returns Emit (separate functions), not COMDAT. The C5 unified-structor comdat
// name must therefore NOT be used; each structor lives in its own self-named
// comdat group, matching OGCG.

struct Base { int b; };
template <typename T>
struct D : virtual Base {
  T x;
  D(T v) : x(v) {}
};
template struct D<int>;

// C1 and C2 are emitted as separate functions in self-named comdat groups.
// LLVM-DAG: $_ZN1DIiEC2Ei = comdat any
// LLVM-DAG: $_ZN1DIiEC1Ei = comdat any
// There must be NO unified C5 structor comdat group for a vbase class.
// LLVM-NOT: $_ZN1DIiEC5Ei
// And C1 must NOT be an alias to C2 here (vbase ctors genuinely differ).
// LLVM-NOT: @_ZN1DIiEC1Ei = {{.*}}alias{{.*}}@_ZN1DIiEC2Ei

// OGCG agrees: self-named groups, no C5.
// OGCG-DAG: $_ZN1DIiEC2Ei = comdat any
// OGCG-DAG: $_ZN1DIiEC1Ei = comdat any
// OGCG-NOT: $_ZN1DIiEC5Ei
