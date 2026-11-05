// RUN: %clang -target aarch64-none-linux-android21 -fclangir \
// RUN:   -fclangir-fat-objects -O2 -c %s -o %t.o
// RUN: llvm-objcopy --dump-section=.cir=%t.cir %t.o /dev/null
// RUN: cir-opt %t.cir | FileCheck %s

// A fat object records where each record and its members are declared. On
// AArch64, va_list is the implicit record __va_list, which has no source
// location; it gets an unknown location instead of asserting: for the record
// and for each of its members.

// CHECK: #[[UNKNOWN:loc[0-9]*]] = loc(unknown)
// CHECK: cir.record_locations = {__va_list = [#[[UNKNOWN]], #[[UNKNOWN]], #[[UNKNOWN]], #[[UNKNOWN]], #[[UNKNOWN]], #[[UNKNOWN]]]}
// CHECK: cir.func{{.*}}@sum

int sum(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  int r = __builtin_va_arg(ap, int);
  __builtin_va_end(ap);
  return r;
}
