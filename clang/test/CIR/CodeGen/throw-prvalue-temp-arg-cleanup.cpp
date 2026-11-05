// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fexceptions -fcxx-exceptions -emit-llvm %s -o - | FileCheck %s

// Regression test: when a `throw` operand's full-expression contains a
// dtor-bearing temporary (here Arg, mirroring the std::allocator temporary of
// `throw std::string("...")`) while an RAII local is live, ClangIR previously
// wrapped __cxa_throw in a synthetic resume-only cleanup and DROPPED the
// enclosing local's destructor on the propagation edge (a leak / observably
// missing destructor). The __cxa_throw cleanup must include the enclosing
// local's destructor, matching classic CodeGen's shared cleanup landing pad.

struct Arg { int x; Arg(int v); ~Arg(); };
struct Thrown { int v; Thrown(Arg a); ~Thrown(); };
struct E { int id; E(int i); ~E(); };

void f() { E marker(500); throw Thrown(Arg(7)); }

// f only throws, so marker's destructor is reachable solely on the throw's
// unwind path; its presence proves the cleanup is attached to __cxa_throw.
// CHECK-LABEL: @_Z1fv
// CHECK: invoke void @__cxa_throw
// CHECK: call void @_ZN1ED1Ev
