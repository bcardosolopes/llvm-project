// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Regression: AArch64 target_clones must use AArch64 FMV mangling
// (._M<feature>...) rather than the x86-only path. This must not crash on
// non-x86 targets.

int __attribute__((target_clones("lse+aes", " default ", "sha2+memtag+sha2")))
foo(void) {
  return 1;
}

// CHECK-LABEL: cir.func{{.*}} @foo._MaesMlse()
// CHECK-LABEL: cir.func{{.*}} @foo.default()
// CHECK-LABEL: cir.func{{.*}} @foo._MmemtagMsha2()

