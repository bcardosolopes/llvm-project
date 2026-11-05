// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-mlir=cir-flat %s -o %t.flat.cir
// RUN: FileCheck --input-file=%t.flat.cir %s -check-prefix=CIR_FLAT
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// A failed dynamic_cast<Derived &> throws std::bad_cast. When the function has a
// local with a non-trivial destructor but NO enclosing try, __cxa_bad_cast must
// still be an invoke whose landing pad runs that destructor before resuming;
// otherwise the local cleanup is dropped as the exception propagates out.

struct Base {
  virtual ~Base();
};

struct Derived : Base {};

struct Guard {
  ~Guard();
};

void ref_cast_cleanup_no_try(Base &b) {
  Guard g;
  Derived &r = dynamic_cast<Derived &>(b);
  (void)r;
}

// The throwing reference cast is wrapped in a synthetic cleanup-only try whose
// unwind handler runs the Guard destructor.
// CIR: cir.func{{.*}} @_Z23ref_cast_cleanup_no_tryR4Base
// CIR:   cir.try synthetic cleanup {
// CIR:     cir.call @__dynamic_cast
// CIR:     cir.if
// CIR:       cir.call exception @__cxa_bad_cast() : () -> ()
// CIR:       cir.unreachable
// CIR:   } unwind {
// CIR:     cir.call @_ZN5GuardD1Ev
// CIR:     cir.resume

// CIR_FLAT: cir.func{{.*}} @_Z23ref_cast_cleanup_no_tryR4Base
// CIR_FLAT:   cir.try_call @__cxa_bad_cast() ^{{.*}}, ^[[LPAD:.*]] : () -> ()
// CIR_FLAT: ^[[LPAD]]:
// CIR_FLAT:   cir.eh.inflight_exception cleanup
// CIR_FLAT:   cir.call @_ZN5GuardD1Ev
// CIR_FLAT:   cir.resume.flat

// LLVM: define{{.*}} @_Z23ref_cast_cleanup_no_tryR4Base
// LLVM: personality ptr @__gxx_personality_v0
// LLVM:   invoke void @__cxa_bad_cast()
// LLVM:          to label %{{.*}} unwind label %[[LPAD:.*]]
// LLVM: [[LPAD]]:
// LLVM:   landingpad { ptr, i32 }
// LLVM:     cleanup
// LLVM:   call void @_ZN5GuardD1Ev
// LLVM:   resume { ptr, i32 }

// OGCG: define{{.*}} @_Z23ref_cast_cleanup_no_tryR4Base
// OGCG: personality ptr @__gxx_personality_v0
// OGCG:   invoke void @__cxa_bad_cast()
// OGCG:          to label %{{.*}} unwind label %[[LPAD:.*]]
// OGCG: [[LPAD]]:
// OGCG:   landingpad { ptr, i32 }
// OGCG:     cleanup
// OGCG:   call void @_ZN5GuardD1Ev
// OGCG:   resume { ptr, i32 }
