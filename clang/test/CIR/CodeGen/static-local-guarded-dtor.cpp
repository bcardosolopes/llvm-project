// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR

// Verify CIR emits a guard pattern for static locals that have a constant
// initializer but a non-trivial destructor.
// Per the Itanium C++ ABI, even if no constructor is needed, we must use
// __cxa_guard_acquire/__cxa_guard_release to guard __cxa_atexit registration.

class a {
public:
  ~a();
};
void b() { static a c; }

// CIR: cir.global {{.*}} @_ZGVZ1bvE1c = #cir.int<0> : !u64i
// CIR: cir.func private @__cxa_guard_acquire
// CIR: cir.func private @__cxa_guard_release
// CIR: cir.func {{.*}} @_Z1bv
// CIR:   cir.if
// CIR:     cir.call @__cxa_guard_acquire
// CIR:     cir.if
// CIR:       cir.call @__cxa_guard_release
