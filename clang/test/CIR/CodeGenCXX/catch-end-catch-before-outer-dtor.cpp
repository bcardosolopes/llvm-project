// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir -fcxx-exceptions -fexceptions
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll -fcxx-exceptions -fexceptions
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Verify that __cxa_end_catch is called BEFORE outer-scope destructors
// when returning from a catch handler. Previously, CIR emitted the
// destructor for objects declared outside the try block before
// __cxa_end_catch, which is incorrect.

void may_throw();

struct Guard {
  int x;
  Guard(int v) : x(v) {}
  ~Guard();
};

// Test 1: Typed catch with return and outer RAII
int test_typed(int x) {
  Guard g(x);
  try {
    may_throw();
    return 1;
  } catch (int e) {
    return e;
  }
  return 0;
}

// CIR-LABEL: @_Z10test_typedi
// CIR:     } catch [type
// CIR:       cir.catch_param
// CIR:       cir.catch_param end
// CIR:       cir.call @_ZN5GuardD1Ev
// CIR:       cir.return

// LLVM-LABEL: @_Z10test_typedi
// LLVM:   call ptr @__cxa_begin_catch
// LLVM:   load i32, ptr
// LLVM:   call void @__cxa_end_catch
// LLVM:   call void @_ZN5GuardD1Ev
// LLVM:   ret i32

// Test 2: Catch-all with return and outer RAII
int test_catchall(int x) {
  Guard g(x);
  try {
    may_throw();
    return 1;
  } catch (...) {
    return -1;
  }
  return 0;
}

// CIR-LABEL: @_Z13test_catchalli
// CIR:     } catch all {
// CIR:       cir.catch_param
// CIR:       cir.catch_param end
// CIR:       cir.call @_ZN5GuardD1Ev
// CIR:       cir.return

// LLVM-LABEL: @_Z13test_catchalli
// LLVM:   call ptr @__cxa_begin_catch
// LLVM:   invoke void @__cxa_end_catch
// LLVM:   call void @_ZN5GuardD1Ev
// LLVM:   ret i32

// Test 3: Multiple outer-scope RAII objects
int test_multiple_raii() {
  Guard g1(1);
  Guard g2(2);
  try {
    may_throw();
    return 1;
  } catch (int e) {
    return e;
  }
  return 0;
}

// CIR-LABEL: @_Z18test_multiple_raiiv
// CIR:     } catch [type
// CIR:       cir.catch_param
// CIR:       cir.catch_param end
// CIR:       cir.call @_ZN5GuardD1Ev
// CIR:       cir.call @_ZN5GuardD1Ev
// CIR:       cir.return

// LLVM-LABEL: @_Z18test_multiple_raiiv
// LLVM:   call ptr @__cxa_begin_catch
// LLVM:   load i32, ptr
// LLVM:   call void @__cxa_end_catch
// LLVM:   call void @_ZN5GuardD1Ev
// LLVM:   call void @_ZN5GuardD1Ev
// LLVM:   ret i32
