// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -fcxx-exceptions \
// RUN:   -fexceptions -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -fcxx-exceptions \
// RUN:   -fexceptions -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that __cxa_end_catch in an inner catch handler is emitted as
// invoke (not call) when there's an outer catch that could handle
// exceptions thrown by the caught object's destructor.

struct A { virtual ~A(); };
struct B : A {};
void may_throw();

int test() {
  try {
    try {
      may_throw();
    } catch (B& b) {
      return 1;
    }
  } catch (A& a) {
    return 2;
  }
  return 0;
}

// Inner catch (B) should use invoke for __cxa_end_catch:
// CIR: invoke void @__cxa_end_catch()
// CIR: call void @__cxa_end_catch()

// OGCG: invoke void @__cxa_end_catch()
// OGCG: call void @__cxa_end_catch()
