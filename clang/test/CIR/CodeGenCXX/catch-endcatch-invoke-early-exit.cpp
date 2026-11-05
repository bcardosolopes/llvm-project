// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -fclangir -emit-llvm %s \
// RUN:   -fcxx-exceptions -fexceptions -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -emit-llvm %s \
// RUN:   -fcxx-exceptions -fexceptions -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// When a catch handler catches a record type (mightThrow=true) and there are
// enclosing normal cleanups (outer RAII objects), the early-exit __cxa_end_catch
// must be emitted as invoke (not call), matching OGCG's EmitRuntimeCallOrInvoke
// behavior. This ensures that if __cxa_end_catch throws (destroying the caught
// exception), the outer cleanup chain still runs.

struct NontrivialDtor {
  ~NontrivialDtor();
};

void may_throw();

// Test 1: Record-type catch with early exit and outer RAII.
// __cxa_end_catch must be invoke because the caught type has a potentially-
// throwing destructor and there's an outer RAII object whose cleanup must run.
int test_record_catch_with_raii() {
  NontrivialDtor obj;
  try {
    may_throw();
  } catch (NontrivialDtor &e) {
    return 42;
  }
  return 0;
}

// CHECK-LABEL: @_Z27test_record_catch_with_raiiv
// CHECK: call ptr @__cxa_begin_catch
// CHECK: invoke void @__cxa_end_catch()
// CHECK: call void @_ZN14NontrivialDtorD1Ev
// CHECK: ret i32

// OGCG-LABEL: @_Z27test_record_catch_with_raiiv
// OGCG: call ptr @__cxa_begin_catch
// OGCG: invoke void @__cxa_end_catch()
// OGCG: call void @_ZN14NontrivialDtorD1Ev

// Test 2: Record-type catch with early exit but NO outer RAII.
// __cxa_end_catch is a plain call because there are no enclosing cleanups
// that need to run if __cxa_end_catch throws.
int test_record_catch_no_raii() {
  try {
    may_throw();
  } catch (NontrivialDtor &e) {
    return 42;
  }
  return 0;
}

// CHECK-LABEL: @_Z25test_record_catch_no_raiiv
// CHECK: call ptr @__cxa_begin_catch
// CHECK: call void @__cxa_end_catch
// CHECK: ret i32

// OGCG-LABEL: @_Z25test_record_catch_no_raiiv
// OGCG: call ptr @__cxa_begin_catch
// OGCG: call void @__cxa_end_catch

// Test 3: Catch-all with early exit and outer RAII.
// __cxa_end_catch must be invoke because catch-all may catch an exception
// with a throwing destructor, and there's an outer cleanup.
int test_catchall_with_raii() {
  NontrivialDtor obj;
  try {
    may_throw();
  } catch (...) {
    return -1;
  }
  return 0;
}

// CHECK-LABEL: @_Z23test_catchall_with_raiiv
// CHECK: call ptr @__cxa_begin_catch
// CHECK: invoke void @__cxa_end_catch()
// CHECK: call void @_ZN14NontrivialDtorD1Ev
// CHECK: ret i32

// OGCG-LABEL: @_Z23test_catchall_with_raiiv
// OGCG: call ptr @__cxa_begin_catch
// OGCG: invoke void @__cxa_end_catch()
// OGCG: call void @_ZN14NontrivialDtorD1Ev

// Test 4: Non-record catch (int) with outer RAII.
// __cxa_end_catch is a plain call (with nounwind) because int has no
// destructor, so __cxa_end_catch cannot throw.
int test_int_catch_with_raii() {
  NontrivialDtor obj;
  try {
    may_throw();
  } catch (int e) {
    return e;
  }
  return 0;
}

// CHECK-LABEL: @_Z24test_int_catch_with_raiiv
// CHECK: call ptr @__cxa_begin_catch
// CHECK: call void @__cxa_end_catch
// CHECK: call void @_ZN14NontrivialDtorD1Ev
// CHECK: ret i32

// OGCG-LABEL: @_Z24test_int_catch_with_raiiv
// OGCG: call ptr @__cxa_begin_catch
// OGCG: call void @__cxa_end_catch
// OGCG: call void @_ZN14NontrivialDtorD1Ev

// Test 5: Continue from catch inside a loop with outer RAII.
// __cxa_end_catch must be invoke — the normal path goes back to the loop,
// but the cleanup landing pad still needs to run the outer destructor.
int test_continue_from_catch() {
  NontrivialDtor obj;
  for (int i = 0; i < 3; i++) {
    try {
      may_throw();
    } catch (NontrivialDtor &e) {
      continue;
    }
  }
  return 0;
}

// CHECK-LABEL: @_Z24test_continue_from_catchv
// CHECK: call ptr @__cxa_begin_catch
// CHECK: invoke void @__cxa_end_catch()
// CHECK: call void @_ZN14NontrivialDtorD1Ev
// CHECK: resume

// OGCG-LABEL: @_Z24test_continue_from_catchv
// OGCG: call ptr @__cxa_begin_catch
// OGCG: invoke void @__cxa_end_catch()
// OGCG: call void @_ZN14NontrivialDtorD1Ev
