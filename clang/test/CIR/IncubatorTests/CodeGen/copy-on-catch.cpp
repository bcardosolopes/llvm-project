// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-cir %s -o %t.cir -fcxx-exceptions -fexceptions
// RUN: FileCheck --input-file=%t.cir %s

// CHECK: cir.func

struct E {};
E e;

void throws() { throw e; }

void bar() {
  try {
    throws();
  } catch (E e) {
  }
}
