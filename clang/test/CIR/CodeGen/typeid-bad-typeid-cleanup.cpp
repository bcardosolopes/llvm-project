// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-mlir=cir-flat %s -o %t.flat.cir
// RUN: FileCheck --input-file=%t.flat.cir %s -check-prefix=CIR_FLAT
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// typeid(*p) with a null p throws std::bad_typeid. When the function has a local
// with a non-trivial destructor but NO enclosing try, __cxa_bad_typeid must
// still be an invoke whose landing pad runs that destructor before resuming;
// otherwise the local cleanup is dropped as the exception propagates out.

namespace std {
class type_info {
public:
  virtual ~type_info();
  const char *name() const;
private:
  const char *__name;
};
}

struct Base {
  virtual ~Base();
};

struct Guard {
  ~Guard();
};

const char *typeid_cleanup_no_try(Base *p) {
  Guard g;
  return typeid(*p).name();
}

// The throwing null check is wrapped in a synthetic cleanup-only try whose
// unwind handler runs the Guard destructor.
// CIR: cir.func{{.*}} @_Z21typeid_cleanup_no_tryP4Base
// CIR: personality(@__gxx_personality_v0)
// CIR:   cir.try synthetic cleanup {
// CIR:     cir.if
// CIR:       cir.call exception @__cxa_bad_typeid() : () -> ()
// CIR:       cir.unreachable
// CIR:   } unwind {
// CIR:     cir.call @_ZN5GuardD1Ev
// CIR:     cir.resume

// CIR_FLAT: cir.func{{.*}} @_Z21typeid_cleanup_no_tryP4Base
// CIR_FLAT: personality(@__gxx_personality_v0)
// CIR_FLAT:   cir.try_call @__cxa_bad_typeid() ^{{.*}}, ^[[LPAD:.*]] : () -> ()
// CIR_FLAT: ^[[LPAD]]:
// CIR_FLAT:   cir.eh.inflight_exception cleanup
// CIR_FLAT:   cir.call @_ZN5GuardD1Ev
// CIR_FLAT:   cir.resume.flat

// LLVM: define{{.*}} @_Z21typeid_cleanup_no_tryP4Base
// LLVM: personality ptr @__gxx_personality_v0
// LLVM:   invoke void @__cxa_bad_typeid()
// LLVM:          to label %{{.*}} unwind label %[[LPAD:.*]]
// LLVM: [[LPAD]]:
// LLVM:   landingpad { ptr, i32 }
// LLVM:     cleanup
// LLVM:   call void @_ZN5GuardD1Ev

// OGCG: define{{.*}} @_Z21typeid_cleanup_no_tryP4Base
// OGCG: personality ptr @__gxx_personality_v0
// OGCG:   invoke void @__cxa_bad_typeid()
// OGCG:          to label %{{.*}} unwind label %[[LPAD:.*]]
// OGCG: [[LPAD]]:
// OGCG:   landingpad { ptr, i32 }
// OGCG:     cleanup
// OGCG:   call void @_ZN5GuardD1Ev
