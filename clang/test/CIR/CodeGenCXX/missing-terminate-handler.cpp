// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck %s < %t.ll

// Test that __clang_call_terminate is generated for cleanup paths
// where __cxa_end_catch could throw during unwinding.
// CIR emits __cxa_end_catch as an invoke with a terminate landing pad,
// satisfying [except.terminate].
//
//

struct RAII {
  int *p;
  RAII(int *p) : p(p) {}
  ~RAII() { *p = 0; }
};

void might_throw();

void test_rethrow() {
  RAII r(nullptr);
  try {
    might_throw();
  } catch (...) {
    throw;
  }
}

// CHECK: @__clang_call_terminate
// CHECK: invoke void @__cxa_end_catch()
// CHECK: landingpad { ptr, i32 }
// CHECK:   catch ptr null
// CHECK: call void @__clang_call_terminate
