// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-mlir=cir-flat %s -o %t.flat.cir
// RUN: FileCheck --input-file=%t.flat.cir %s -check-prefix=CIR_FLAT
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Verify that a failed dynamic_cast<Derived &> inside a try block throws
// std::bad_cast that routes to the catch handler instead of std::terminate.

struct Base {
  virtual ~Base();
};

struct D1 : Base {};
struct D2 : Base {};

// A failed reference dynamic_cast inside a try-catch must produce an invoke
// to __cxa_bad_cast so the exception routes to the catch handler.
int ref_cast_in_try(Base &b) {
  try {
    D2 &r = dynamic_cast<D2 &>(b);
    return 1;
  } catch (...) {
    return 0;
  }
}

// A reference dynamic_cast can throw bad_cast, so the function must NOT
// be marked nounwind — even though all explicit calls in the body are nothrow.
// CIR: cir.func{{.*}} @_Z15ref_cast_in_tryR4Base
// CIR-NOT: nothrow
// CIR: personality(@__gxx_personality_v0)
// CIR:   cir.try {
// CIR:     cir.call @__dynamic_cast
// CIR:     cir.if
// CIR:       cir.call exception @__cxa_bad_cast() : () -> ()
// CIR:       cir.unreachable
// CIR:   } catch all {

// CIR_FLAT: cir.func{{.*}} @_Z15ref_cast_in_tryR4Base
// CIR_FLAT: personality(@__gxx_personality_v0)
// CIR_FLAT:   cir.try_call @__cxa_bad_cast() ^{{.*}}, ^[[LPAD:.*]] : () -> ()
// CIR_FLAT: ^[[LPAD]]:
// CIR_FLAT:   %{{.*}}, %{{.*}} = cir.eh.inflight_exception catch_all
// CIR_FLAT:   cir.catch_param begin

// LLVM: define{{.*}} i32 @_Z15ref_cast_in_tryR4Base
// LLVM-NOT: nounwind
// LLVM: personality ptr @__gxx_personality_v0
// LLVM:   invoke void @__cxa_bad_cast()
// LLVM:          to label %{{.*}} unwind label %[[LPAD:.*]]
// LLVM: [[LPAD]]:
// LLVM:   landingpad { ptr, i32 }
// LLVM:     catch ptr null
// LLVM:   call ptr @__cxa_begin_catch

// OGCG: define{{.*}} i32 @_Z15ref_cast_in_tryR4Base
// OGCG: personality ptr @__gxx_personality_v0
// OGCG:   invoke void @__cxa_bad_cast()
// OGCG:          to label %{{.*}} unwind label %[[LPAD:.*]]
// OGCG: [[LPAD]]:
// OGCG:   landingpad { ptr, i32 }
// OGCG:     catch ptr null
// OGCG:   call ptr @__cxa_begin_catch
