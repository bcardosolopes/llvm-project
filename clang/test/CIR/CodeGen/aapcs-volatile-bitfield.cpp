// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fclangir -emit-cir -faapcs-bitfield-width %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that volatile bitfield loads use the correct AAPCS volatile container
// size. Previously, the load path checked volatileOffset instead of
// volatileStorageSize, causing mismatched volatile access widths between
// loads and stores.

struct S {
  volatile int x : 8;
  volatile int y : 8;
};

// CHECK-LABEL: @_Z6read_xP1S
int read_x(S *s) {
  // CHECK: cir.get_bitfield {{.*}} {is_volatile}
  return s->x;
}

// CHECK-LABEL: @_Z7write_xP1Si
void write_x(S *s, int val) {
  // CHECK: cir.set_bitfield {{.*}} {is_volatile}
  s->x = val;
}
