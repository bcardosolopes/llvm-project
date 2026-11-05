// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -fcxx-exceptions -fexceptions %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

// Tests that noexcept functions correctly emit terminate handlers using
// the shared __clang_call_terminate helper function, matching OGCG.
// Per C++ [except.terminate], if an exception escapes a noexcept function,
// std::terminate() must be called.

// Check that __clang_call_terminate helper is defined (CIR emits it before
// test functions; OGCG emits it after test_basic).
// LLVM: define linkonce_odr hidden void @__clang_call_terminate(ptr {{.*}}) {{.*}}comdat
// LLVM:   call ptr @__cxa_begin_catch({{.*}}) #[[NOUNWIND:[0-9]+]]
// LLVM:   call void @_ZSt9terminatev() #[[NORETURN_NOUNWIND:[0-9]+]]
// LLVM-NEXT: unreachable

void may_throw();

// Basic noexcept function.
void test_basic() noexcept {
    may_throw();
}

// CIR: cir.func {{.*}}@_Z10test_basicv()
// CIR:   cir.try synthetic {
// CIR:     cir.call exception @_Z9may_throwv()
// CIR:   } catch all {
// CIR:     cir.catch_param
// CIR:     cir.call @_ZSt9terminatev()
// CIR:     cir.unreachable
// CIR:   }

// LLVM: define {{.*}}void @_Z10test_basicv()
// LLVM:   invoke void @_Z9may_throwv()
// LLVM:     to label %{{.*}} unwind label %[[LP:.*]]
// LLVM: [[LP]]:
// LLVM-NEXT: %[[EXN:.*]] = landingpad { ptr, i32 }
// LLVM-NEXT:   catch ptr null
// LLVM-NEXT: %[[SLOT:.*]] = extractvalue { ptr, i32 } %[[EXN]], 0
// LLVM-NEXT: call void @__clang_call_terminate(ptr %[[SLOT]]) #[[NORETURN_NOUNWIND]]
// LLVM-NEXT: unreachable

// OGCG: define {{.*}}void @_Z10test_basicv()
// OGCG:   invoke void @_Z9may_throwv()
// OGCG:     to label %{{.*}} unwind label %[[LP:.*]]
// OGCG: [[LP]]:
// OGCG:   landingpad { ptr, i32 }
// OGCG:     catch ptr null
// OGCG:   call void @__clang_call_terminate(

// OGCG __clang_call_terminate helper definition (emitted after test_basic).
// OGCG: define linkonce_odr hidden void @__clang_call_terminate(
// OGCG:   call ptr @__cxa_begin_catch(
// OGCG-NEXT: call void @_ZSt9terminatev()
// OGCG-NEXT: unreachable

// Noexcept function with RAII cleanup — destructor must run before terminate.
struct Guard { ~Guard(); };

void test_with_raii() noexcept {
    Guard g;
    may_throw();
}

// LLVM: define {{.*}}void @_Z14test_with_raiiv()
// LLVM:   invoke void @_Z9may_throwv()
// LLVM:     to label %{{.*}} unwind label %[[LP2:.*]]
// LLVM: [[LP2]]:
// LLVM-NEXT: %[[EXN2:.*]] = landingpad { ptr, i32 }
// LLVM-NEXT:   catch ptr null
// LLVM-NEXT: %[[SLOT2:.*]] = extractvalue { ptr, i32 } %[[EXN2]], 0
// LLVM-NEXT: call void @__clang_call_terminate(ptr %[[SLOT2]])
// LLVM-NEXT: unreachable

// Note: OGCG doesn't run RAII destructors before terminate (both behaviors
// are valid per C++ [except.terminate] — stack unwinding is implementation-
// defined). OGCG uses the terminate landing pad directly, skipping cleanups.
// OGCG: define {{.*}}void @_Z14test_with_raiiv()
// OGCG:   invoke void @_Z9may_throwv()
// OGCG:     to label %{{.*}} unwind label %[[LP2:.*]]
// OGCG: [[LP2]]:
// OGCG:   landingpad { ptr, i32 }
// OGCG:     catch ptr null
// OGCG:   call void @__clang_call_terminate(

// Throw expression directly inside noexcept — the __cxa_throw invoke
// should have a terminate landing pad, not a cleanup landing pad.
void test_throw_noexcept() noexcept {
    throw 42;
}

// CIR: cir.func {{.*}}@_Z19test_throw_noexceptv()
// CIR:   cir.try synthetic {
// CIR:     cir.throw
// CIR:   } catch all {
// CIR:     cir.catch_param
// CIR:     cir.call @_ZSt9terminatev()
// CIR:     cir.unreachable
// CIR:   }

// LLVM: define {{.*}}void @_Z19test_throw_noexceptv()
// LLVM:   invoke void @__cxa_throw(
// LLVM:     to label %{{.*}} unwind label %[[LP3:.*]]
// LLVM: [[LP3]]:
// LLVM-NEXT: %[[EXN3:.*]] = landingpad { ptr, i32 }
// LLVM-NEXT:   catch ptr null
// LLVM-NEXT: %[[SLOT3:.*]] = extractvalue { ptr, i32 } %[[EXN3]], 0
// LLVM-NEXT: call void @__clang_call_terminate(ptr %[[SLOT3]])
// LLVM-NEXT: unreachable

// OGCG: define {{.*}}void @_Z19test_throw_noexceptv()
// OGCG:   invoke void @__cxa_throw(
// OGCG:     to label %{{.*}} unwind label %[[LP3:.*]]
// OGCG: [[LP3]]:
// OGCG:   landingpad { ptr, i32 }
// OGCG:     catch ptr null
// OGCG:   call void @__clang_call_terminate(

// try-catch inside noexcept — unhandled exceptions must terminate,
// not resume past the noexcept boundary.
void test_try_in_noexcept() noexcept {
    try {
        may_throw();
    } catch (int) {
    }
}

// Verify landing pad and type dispatch are in the same block (no
// forwarding block between landing pad and catch dispatcher).
// LLVM: define {{.*}}void @_Z20test_try_in_noexceptv()
// LLVM:   invoke void @_Z9may_throwv()
// LLVM:     to label %{{.*}} unwind label %[[LP4:.*]]
// LLVM: [[LP4]]:
// LLVM-NEXT: %[[EXN4:.*]] = landingpad { ptr, i32 }
// LLVM-NEXT:   catch ptr @_ZTIi
// LLVM-NEXT:   catch ptr null
// LLVM-NEXT: %{{.*}} = extractvalue { ptr, i32 } %[[EXN4]], 0
// LLVM-NEXT: %{{.*}} = extractvalue { ptr, i32 } %[[EXN4]], 1
// LLVM:   call void @__clang_call_terminate(

// OGCG: define {{.*}}void @_Z20test_try_in_noexceptv()
// OGCG:   landingpad { ptr, i32 }
// OGCG:     catch ptr @_ZTIi
// OGCG:     catch ptr null
// OGCG:   call void @__clang_call_terminate(

// Verify that noreturn and nounwind are set correctly on call-site attributes.
// LLVM-DAG: attributes #[[NOUNWIND]] = {{{.*}}nounwind{{.*}}}
// LLVM-DAG: attributes #[[NORETURN_NOUNWIND]] = {{{.*}}noreturn nounwind{{.*}}}
