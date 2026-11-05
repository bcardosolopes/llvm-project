// RUN: %clang_cc1 -std=c23 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// Verify __builtin_c23_va_start with 1 arg (no count) compiles correctly.
typedef __builtin_va_list va_list;
int sum(int count, ...) {
  va_list ap;
  __builtin_c23_va_start(ap);
  int total = 0;
  for (int i = 0; i < count; i++)
    total += __builtin_va_arg(ap, int);
  __builtin_va_end(ap);
  return total;
}
// CHECK: cir.va_start
// CHECK: cir.va_end
