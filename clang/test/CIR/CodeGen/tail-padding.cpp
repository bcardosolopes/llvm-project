// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Verify that [[no_unique_address]] fields in classes with virtual bases
// use the base subobject type in the record layout, allowing tail padding
// reuse by virtual bases.

struct A { char c; A(const A&); };
struct B { int n; char c[3]; ~B(); };
struct D : virtual A { [[no_unique_address]] B b; };

D f(D d) { return d; }

// CIR: !rec_B2Ebase = !cir.record<struct "B.base" packed {!s32i, !cir.array<!s8i x 3>}>
// CIR: !rec_D = !cir.record<struct "D" packed {!cir.vptr, !rec_B2Ebase, !rec_A}>

// LLVM: define {{.*}} @_Z1f1D
