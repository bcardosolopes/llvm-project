// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-mlir=cir-flat %s -o %t.flat.cir
// RUN: FileCheck --input-file=%t.flat.cir %s -check-prefix=CIR_FLAT
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Verify that typeid(*null_ptr) inside a try block throws std::bad_typeid
// that routes to the catch handler instead of std::terminate.

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

// typeid(*ptr) with null ptr inside try-catch must produce an invoke
// to __cxa_bad_typeid so the exception routes to the catch handler.
int typeid_null_in_try(Base *ptr) {
  try {
    const std::type_info &ti = typeid(*ptr);
    (void)ti;
    return 1;
  } catch (...) {
    return 0;
  }
}

// CIR: cir.func{{.*}} @_Z18typeid_null_in_tryP4Base
// CIR: personality(@__gxx_personality_v0)
// CIR:   cir.try {
// CIR:     cir.if
// CIR:       cir.call exception @__cxa_bad_typeid() : () -> ()
// CIR:       cir.unreachable
// CIR:   } catch all {

// CIR_FLAT: cir.func{{.*}} @_Z18typeid_null_in_tryP4Base
// CIR_FLAT: personality(@__gxx_personality_v0)
// CIR_FLAT:   cir.try_call @__cxa_bad_typeid() ^{{.*}}, ^[[LPAD:.*]] : () -> ()
// CIR_FLAT: ^[[LPAD]]:
// CIR_FLAT:   %{{.*}}, %{{.*}} = cir.eh.inflight_exception catch_all
// CIR_FLAT:   cir.catch_param begin

// LLVM: define{{.*}} i32 @_Z18typeid_null_in_tryP4Base
// LLVM: personality ptr @__gxx_personality_v0
// LLVM:   invoke void @__cxa_bad_typeid()
// LLVM:          to label %{{.*}} unwind label %[[LPAD:.*]]
// LLVM: [[LPAD]]:
// LLVM:   landingpad { ptr, i32 }
// LLVM:     catch ptr null
// LLVM:   call ptr @__cxa_begin_catch

// OGCG: define{{.*}} i32 @_Z18typeid_null_in_tryP4Base
// OGCG: personality ptr @__gxx_personality_v0
// OGCG:   invoke void @__cxa_bad_typeid()
// OGCG:          to label %{{.*}} unwind label %[[LPAD:.*]]
// OGCG: [[LPAD]]:
// OGCG:   landingpad { ptr, i32 }
// OGCG:     catch ptr null
// OGCG:   call ptr @__cxa_begin_catch
