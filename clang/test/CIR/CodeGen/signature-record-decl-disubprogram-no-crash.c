// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -fclangir -fclangir-mem2reg \
// RUN:   -emit-obj -Oz -gkey-instructions -debug-info-kind=constructor \
// RUN:   -dwarf-version=4 -debugger-tuning=lldb %s -o /dev/null

struct S {
  int x;
  int y;
};

extern void sink(struct S s);

int f(struct S *s) {
  sink(*s);
  return s->x + s->y;
}
