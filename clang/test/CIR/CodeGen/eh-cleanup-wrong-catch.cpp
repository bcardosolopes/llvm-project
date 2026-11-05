// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll -fexceptions -fcxx-exceptions
// RUN: FileCheck --input-file=%t.ll %s
//
// When a try/catch block catches the wrong exception type and the
// exception propagates, destructors for locals declared OUTSIDE the try block
// must still run. This requires the landing pad to have the `cleanup` clause.

struct RAII {
  int id;
  RAII(int i);
  ~RAII();
};

void thrower();

void test_wrong_catch_with_local() {
  RAII r(1);
  try {
    thrower();
  } catch (double d) {
  }
}

// The landing pad must include `cleanup` because RAII r(1) needs destruction
// when the exception doesn't match `double`.
// CHECK: landingpad
// CHECK-NEXT: cleanup
