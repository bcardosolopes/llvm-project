// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-cir %s -o %t.cir -fcxx-exceptions -fexceptions
// RUN: FileCheck --input-file=%t.cir %s

// Previously crashed: catch-by-value with copy on aarch64 triple.
// Now compiles correctly, generating the copy constructor for the caught type.

// CHECK: cir.func
// CHECK: cir.try

struct E {};
E e;

void throws() { throw e; }

void bar() {
  try {
    throws();
  } catch (E e) {
  }
}
