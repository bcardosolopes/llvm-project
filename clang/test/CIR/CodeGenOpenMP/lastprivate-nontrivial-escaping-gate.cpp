// RUN: %clang_cc1 -fopenmp -fcxx-exceptions -fexceptions -x c++ -std=c++17 \
// RUN:   -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -verify %s -o %t.cir

// The lastprivate copy-back on the last iteration calls the copy-assignment
// operator, which may throw; its unwind edge lands on the original variable's
// destructor cleanup, which predates the outlined region. That cleanup is
// compiler-generated region teardown, not a user exception escaping the region,
// so the escaping-exception gate must not fire on it. With C++ exceptions
// enabled (as the driver does by default) this previously produced a spurious
// "exception escaping an OpenMP region" NYI. A negative case confirms the gate
// still fires for a user throwing-call that genuinely escapes the region.

struct W {
  int v;
  W() : v(0) {}
  W(const W &o) : v(o.v) {}
  W &operator=(const W &);
  ~W();
};

// The lastprivate copy-back must NOT be gated: no diagnostic here.
void lastprivateNonTrivial() {
  W last;
#pragma omp parallel for lastprivate(last) num_threads(4)
  for (int i = 0; i < 10; ++i)
    last.v = i;
}

void mayThrow();

// A user call that can throw, inside an outlined region wrapped by an
// out-of-region handler, DOES escape and must still be gated. This guards
// against the copy-back suppression being scoped too broadly.
void escapingCallStillGated() {
  try {
#pragma omp parallel num_threads(4)
    {
      // expected-error@+1 {{ClangIR code gen Not Yet Implemented: exception escaping an OpenMP region}}
      mayThrow();
    }
  } catch (...) {
  }
}
