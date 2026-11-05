// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s
// cir.byte_swap verification error on 8-bit integer operands.
// Fixed: __builtin_bswapg on 8-bit types now returns identity (no-op).

char test_bswap_char(char x) {
  return __builtin_bswapg(x);
}

// CHECK: define {{.*}}@_Z15test_bswap_charc
// CHECK: ret i8 %
