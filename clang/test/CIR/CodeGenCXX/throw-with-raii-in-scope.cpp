// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj -fcxx-exceptions -fexceptions %s -o %t.o
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
//
// `throw` in a scope where an RAII object with non-trivial destructor is alive.
// Previously crashed CIR verification with "block with no terminator".
// Fixed: synthetic TryOp wraps the throw with cleanup handlers.

struct Guard {
  ~Guard() {}
};

void test() {
  Guard g;
  throw 42;
}

// CIR: cir.try synthetic cleanup {
// CIR:   cir.throw
// CIR:   cir.unreachable
// CIR: } unwind {
// CIR:   cir.call @_ZN5GuardD1Ev
// CIR:   cir.resume
