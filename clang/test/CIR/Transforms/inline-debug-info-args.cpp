// A callee parameter's spill that the CIR inliner moves into the caller is
// stored from whatever the call passed -- here the caller's own sret
// argument. It must not be taken for a parameter of the caller: that gave the
// callee's subprogram a second "dst" with argument number 1 and the caller's
// type, and AsmPrinter asserts on two variables with one argument number.
//
// RUN: %clang_cc1 -triple aarch64-linux-android28 -fclangir -O2 \
// RUN:     -fclangir-mem2reg -fclangir-inline -fclangir-inline-threshold=16 \
// RUN:     -fcxx-exceptions -fexceptions -debug-info-kind=limited \
// RUN:     -dwarf-version=4 -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-android28 -fclangir -O2 \
// RUN:     -fclangir-mem2reg -fclangir-inline -fclangir-inline-threshold=16 \
// RUN:     -fcxx-exceptions -fexceptions -debug-info-kind=limited \
// RUN:     -dwarf-version=4 -emit-obj %s -o /dev/null

typedef unsigned long size_t;
void *copy(void *const dst, const void *src, size_t n) {
  return __builtin_memcpy(dst, src, n);
}
struct P {
  void *p;
  P();
  ~P();
};
P extract(P &&ptr) {
  P ret;
  copy(&ret, &ptr, sizeof(P));
  return ret;
}

// CHECK: !DILocalVariable(name: "dst", arg: 1, scope: ![[COPY_SP:[0-9]+]]
// CHECK-NOT: !DILocalVariable(name: "dst", arg: 1,
// CHECK: !DILocalVariable(name: "ptr", arg: 2,
