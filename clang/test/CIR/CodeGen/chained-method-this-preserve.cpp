// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -O2 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test: CIR must preserve the 'this' pointer across inlined
// function calls in argument expressions of chained method calls.
//
// Previously, CIR would generate 'undef' for the 'this' argument of the
// second method call in a chain like:
//   obj.method1(a).method2(compute_arg())
// when compute_arg() was inlined and contained function calls.
//
// This caused a SIGSEGV in abseil LOG macros, where the LOG
// macro expands to chained operator<< calls with inlined argument
// computations.

struct VBase {
  virtual ~VBase() {}
  int vdata[8] = {};
};

struct Logger : public VBase {
  char *ptr;
  int size;

  Logger(char *p, int n) : ptr(p), size(n) {}

  __attribute__((noinline)) Logger &write_str(const char *s, int len) {
    for (int i = 0; i < len && i < size; i++)
      ptr[i] = s[i];
    return *this;
  }

  __attribute__((noinline)) Logger &write_long(long val) {
    if (size > 0)
      ptr[0] = '0' + (val % 10);
    return *this;
  }
};

// Not noinline: these get inlined, which triggers the bug.
bool check_a();
bool check_b();
bool check_c();

inline long compute() {
  if (check_a())
    return 10;
  if (check_b())
    return 20;
  if (check_c())
    return 30;
  return 1;
}

// CHECK-LABEL: define {{.*}} @_Z4testR6Logger
// The return value from write_str must be passed as the first argument to
// write_long -- NOT 'undef'. The inlined compute() calls (check_a, check_b,
// check_c) appear between write_str and write_long but must not cause the
// 'this' pointer to be lost.
// CHECK: %[[RET:[0-9]+]] = {{.*}}call {{.*}} @_ZN6Logger9write_strEPKci
// CHECK: call {{.*}} @_Z7check_av
// CHECK: call {{.*}} @_ZN6Logger10write_longEl(ptr {{.*}} %[[RET]],
void test(Logger &log) {
  log.write_str("x", 1).write_long(compute());
}
