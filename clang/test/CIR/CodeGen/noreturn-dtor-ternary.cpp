// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -O1 %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Verify that a noreturn destructor in a ternary expression inside a
// while(false) loop compiles at -O1 without "cir.yield must be the last
// operation in the parent block" verification failure.
//
// This is the pattern used by glog's DCHECK/CHECK macros. At -O1, lifetime
// markers are enabled, and the cir.lifetime.end cleanup for temporaries must
// not be emitted into the dead block created after cir.unreachable from the
// noreturn destructor.

struct Fatal {
  ~Fatal() __attribute__((noreturn));
};
struct Voidify {
  void operator&(Fatal) {}
};
void test(bool cond) {
  while (false) cond ? (void)0 : Voidify() & Fatal();
}

// CHECK: cir.func {{.*}}@_Z4testb
// CHECK:   cir.return
