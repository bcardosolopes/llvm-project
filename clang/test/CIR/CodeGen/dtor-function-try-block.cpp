// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fcxx-exceptions -fexceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

void may_throw();

// Destructor function-try-block with catch-all.
// Per [except.handle]p11, reaching the end of a handler in a destructor
// function-try-block causes the exception to be implicitly rethrown.
struct CatchAll {
  ~CatchAll() try {
    may_throw();
  } catch (...) {
  }
};

// CIR-LABEL: cir.func {{.*}}@_ZN8CatchAllD1Ev
// CIR: cir.try {
// CIR:   cir.call exception @_Z9may_throwv() : () -> ()
// CIR:   cir.yield
// CIR: } catch all {
// CIR:   cir.catch_param
// CIR:   cir.call exception @__cxa_rethrow()
// CIR:   cir.unreachable
// CIR: }

// LLVM-LABEL: @_ZN8CatchAllD1Ev
// LLVM: personality ptr @__gxx_personality_v0
// LLVM: invoke void @_Z9may_throwv()
// LLVM:         to label %[[CONT:[0-9]+]] unwind label %[[LPAD:[0-9]+]]
// LLVM: [[LPAD]]:
// LLVM:   landingpad { ptr, i32 }
// LLVM:     catch ptr null
// LLVM: call ptr @__cxa_begin_catch
// LLVM: invoke void @__cxa_rethrow()
// LLVM:   to label %{{.*}} unwind label %[[RETHROW_LP:[0-9]+]]
// LLVM: [[RETHROW_LP]]:
// LLVM:   landingpad { ptr, i32 }
// LLVM:     cleanup
// LLVM:   invoke void @__cxa_end_catch()
// LLVM:   resume { ptr, i32 }
struct TypedCatch {
  ~TypedCatch() try {
    may_throw();
  } catch (int) {
  } catch (...) {
  }
};

// CIR-LABEL: cir.func {{.*}}@_ZN10TypedCatchD1Ev
// CIR: cir.try {
// CIR:   cir.call exception @_Z9may_throwv() : () -> ()
// CIR:   cir.yield
// CIR: } catch [type #cir.global_view<@_ZTIi>
// CIR:   cir.catch_param
// CIR:   cir.call exception @__cxa_rethrow()
// CIR:   cir.unreachable
// CIR: } catch all {
// CIR:   cir.catch_param
// CIR:   cir.call exception @__cxa_rethrow()
// CIR:   cir.unreachable
// CIR: }

// LLVM-LABEL: @_ZN10TypedCatchD1Ev
// LLVM: personality ptr @__gxx_personality_v0
// LLVM: invoke void @_Z9may_throwv()
// LLVM:         to label %[[CONT:[0-9]+]] unwind label %[[LPAD:[0-9]+]]
// LLVM: [[LPAD]]:
// LLVM:   landingpad { ptr, i32 }
// LLVM:     catch ptr @_ZTIi
// LLVM: call i32 @llvm.eh.typeid.for
// LLVM: call ptr @__cxa_begin_catch
// LLVM: invoke void @__cxa_rethrow()
// LLVM: call ptr @__cxa_begin_catch
// LLVM: invoke void @__cxa_rethrow()

void test() {
  TypedCatch t;
  CatchAll c;
}
