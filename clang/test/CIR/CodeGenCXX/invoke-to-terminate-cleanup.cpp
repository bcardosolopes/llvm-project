// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Per C++ [except.terminate], if a destructor throws during stack
// unwinding, std::terminate() must be called. When a destructor is explicitly
// noexcept(false), cleanup code during unwinding must wrap its call in
//   invoke void @Dtor() to label %ok unwind label %terminate.lpad
// where the terminate landing pad calls __clang_call_terminate.

// A destructor that might throw (explicitly noexcept(false)).
// Without this, C++11+ destructors are implicitly noexcept and OGCG
// correctly emits a plain `call` with nounwind.
struct Throwing {
  ~Throwing() noexcept(false);
};

struct NonPOD {
  NonPOD(const NonPOD&); // may throw
  ~NonPOD();
};

struct Mixed {
  Throwing t;
  NonPOD np;
};

void test(const Mixed& m) { Mixed m2 = m; }

// In _ZN5MixedC2ERKS_, the NonPOD copy constructor is wrapped in an invoke
// with a cleanup landing pad. Inside that cleanup, the Throwing destructor
// must be called via invoke-to-terminate.

// CIR-LABEL: define {{.*}} @_ZN5MixedC2ERKS_
// CIR: invoke void @_ZN8ThrowingD1Ev(
// CIR-SAME: )
// CIR-NEXT: to label %{{.*}} unwind label %[[TERMLPAD:.*]]
// CIR: [[TERMLPAD]]:
// CIR-NEXT: landingpad { ptr, i32 }
// CIR-NEXT: catch ptr null
// CIR-NEXT: extractvalue { ptr, i32 }
// CIR-NEXT: call void @__clang_call_terminate(
// CIR-NEXT: unreachable

// OGCG-LABEL: define {{.*}} @_ZN5MixedC2ERKS_
// OGCG: invoke void @_ZN8ThrowingD1Ev(
// OGCG-SAME: )
// OGCG-NEXT: to label %{{.*}} unwind label %[[TERMLPAD:.*]]
// OGCG: [[TERMLPAD]]:
// OGCG-NEXT: landingpad { ptr, i32 }
// OGCG-NEXT: catch ptr null
// OGCG-NEXT: extractvalue { ptr, i32 }
// OGCG-NEXT: call void @__clang_call_terminate(
// OGCG-NEXT: unreachable
