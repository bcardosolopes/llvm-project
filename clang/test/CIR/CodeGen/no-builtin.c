// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

typedef unsigned long size_t;
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);

// No attribute — builtins should be emitted as CIR ops.
// CHECK-LABEL: @no_attribute
// CHECK: cir.libc.memset
// CHECK: cir.libc.memcpy
void no_attribute(char *s, char *d, size_t n) {
  memset(s, 2, n);
  memcpy(d, s, n);
}

// Specific builtin disabled — only that one should be a regular call.
// CHECK-LABEL: @specific_no_builtin
// CHECK: cir.call @memset
// CHECK: cir.libc.memcpy
__attribute__((no_builtin("memset")))
void specific_no_builtin(char *s, char *d, size_t n) {
  memset(s, 2, n);
  memcpy(d, s, n);
}

// Wildcard no_builtin — all builtins should be regular calls.
// CHECK-LABEL: @wildcard_no_builtin
// CHECK: cir.call @memset
// CHECK: cir.call @memcpy
__attribute__((no_builtin))
void wildcard_no_builtin(char *s, char *d, size_t n) {
  memset(s, 2, n);
  memcpy(d, s, n);
}
