// RUN: %clang_cc1 %s -triple i386-unknown-linux-gnu -mregparm 3 -emit-llvm -o - | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 %s -triple i386-unknown-linux-gnu -mregparm 3 -fclangir -emit-llvm -o - | FileCheck --check-prefix=CHECK-CIR %s %}

void *memcpy(void *dest, const void *src, unsigned int n);

void use_builtin_memcpy(void *dest, const void *src, unsigned int n) {
  __builtin_memcpy(dest, src, n);
}

void use_memcpy(void *dest, const void *src, unsigned int n) {
  memcpy(dest, src, n);
}

//CHECK: !{i32 1, !"NumRegisterParameters", i32 3}
//CHECK-CIR: !{i32 1, !"NumRegisterParameters", i32 3}
