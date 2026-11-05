// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// A failed reference dynamic_cast throws std::bad_cast; typeid(*p) on a null
// polymorphic pointer throws std::bad_typeid. When either is inside a user
// try/catch with a local that has a non-trivial destructor in scope, the
// __cxa_bad_cast / __cxa_bad_typeid invoke must unwind to a landing pad that
// runs that destructor before dispatching to the catch — otherwise the local
// cleanup is silently dropped as the exception propagates (an RAII violation).

namespace std {
class type_info {
public:
  virtual ~type_info();
  const char *name() const;

private:
  const char *__name;
};
} // namespace std

struct Base {
  virtual ~Base();
};

struct D1 : Base {};
struct D2 : Base {};

struct Guard {
  ~Guard();
};

// The __cxa_bad_cast call carries a per-call cleanup region running ~Guard, so
// FlattenCFG threads the destructor onto its unwind edge.
// CIR: cir.func{{.*}} @_Z23bad_cast_cleanup_in_tryR4Base
// CIR:   cir.try cleanup {
// CIR:     cir.call @__dynamic_cast
// CIR:     cir.if
// CIR:       cir.call exception @__cxa_bad_cast() : () -> (){{.*}}cleanup {
// CIR:         cir.call @_ZN5GuardD1Ev
// CIR:       }
// CIR:       cir.unreachable
// CIR:   } catch all {
int bad_cast_cleanup_in_try(Base &b) {
  try {
    Guard g;
    D2 &r = dynamic_cast<D2 &>(b);
    (void)r;
    return 1;
  } catch (...) {
    return 0;
  }
}

// LLVM: define{{.*}} i32 @_Z23bad_cast_cleanup_in_tryR4Base
// LLVM:   invoke void @__cxa_bad_cast()
// LLVM:          to label %{{.*}} unwind label %[[LPAD:.*]]
// LLVM: [[LPAD]]:
// LLVM:   landingpad { ptr, i32 }
// LLVM:     cleanup
// LLVM:     catch ptr null
// LLVM:   call void @_ZN5GuardD1Ev

// OGCG: define{{.*}} i32 @_Z23bad_cast_cleanup_in_tryR4Base
// OGCG:   invoke void @__cxa_bad_cast()
// OGCG:          to label %{{.*}} unwind label %[[LPAD:.*]]
// OGCG: [[LPAD]]:
// OGCG:   landingpad { ptr, i32 }
// OGCG:     catch ptr null
// OGCG:   call void @_ZN5GuardD1Ev

// The __cxa_bad_typeid call carries a per-call cleanup region running ~Guard.
// CIR: cir.func{{.*}} @_Z25bad_typeid_cleanup_in_tryP4Base
// CIR:   cir.try cleanup {
// CIR:     cir.if
// CIR:       cir.call exception @__cxa_bad_typeid() : () -> (){{.*}}cleanup {
// CIR:         cir.call @_ZN5GuardD1Ev
// CIR:       }
// CIR:       cir.unreachable
// CIR:   } catch all {
const char *bad_typeid_cleanup_in_try(Base *p) {
  try {
    Guard g;
    return typeid(*p).name();
  } catch (...) {
    return "";
  }
}

// LLVM: define{{.*}} @_Z25bad_typeid_cleanup_in_tryP4Base
// LLVM:   invoke void @__cxa_bad_typeid()
// LLVM:          to label %{{.*}} unwind label %[[LPAD2:.*]]
// LLVM: [[LPAD2]]:
// LLVM:   landingpad { ptr, i32 }
// LLVM:     cleanup
// LLVM:     catch ptr null
// LLVM:   call void @_ZN5GuardD1Ev

// OGCG: define{{.*}} @_Z25bad_typeid_cleanup_in_tryP4Base
// OGCG:   invoke void @__cxa_bad_typeid()
// OGCG:          to label %{{.*}} unwind label %[[LPAD2:.*]]
// OGCG: [[LPAD2]]:
// OGCG:   landingpad { ptr, i32 }
// OGCG:     catch ptr null
// OGCG:   call void @_ZN5GuardD1Ev
