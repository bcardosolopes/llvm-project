// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -std=c++20 -fclangir -emit-cir -O1 -o /dev/null %s 2>&1 | FileCheck --allow-empty %s
//
// while(false) body with ternary containing a temporary with
// noreturn destructor causes cir.yield to not be the last operation
// in its parent block at -O1. This is the pattern used by glog's
// DCHECK/CHECK macros.

struct Fatal {
  ~Fatal() __attribute__((noreturn));
};
struct Voidify {
  void operator&(Fatal) {}
};
void test(bool cond) {
  while (false) cond ? (void)0 : Voidify() & Fatal();
}

// CHECK-NOT: cir.yield{{.*}}must be the last operation
