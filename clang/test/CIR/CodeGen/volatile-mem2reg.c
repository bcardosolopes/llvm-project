// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN:   -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Regression test: mem2reg must not promote (and thereby delete) the accesses
// of a volatile local. A volatile load/store is an externally observable side
// effect (MMIO, signals, longjmp); eliding it is a miscompile. Previously the
// PromotableMemOpInterface impls for cir.load/cir.store ignored the volatile
// flag, so the whole alloca was promoted to SSA and every volatile access was
// dropped. The non-volatile local must still be promoted.

int test(int a) {
  volatile int v = a;
  v = v + 1;
  int n = a;     // non-volatile: should be promoted away
  n = n + 5;
  return v + n;
}

// The volatile slot survives mem2reg: its alloca and volatile load/store remain.
// CHECK-LABEL: cir.func {{.*}}@test
// CHECK:         %[[V:.*]] = cir.alloca {{.*}}["v", init]
// CHECK:         cir.store volatile {{.*}}, %[[V]]
// CHECK:         cir.load volatile {{.*}}%[[V]]
// CHECK:         cir.store volatile {{.*}}, %[[V]]
// CHECK:         cir.load volatile {{.*}}%[[V]]
// The non-volatile local 'n' is promoted: no second alloca remains.
// CHECK-NOT:     ["n", init]

// LLVM-LABEL: define {{.*}}@test
// LLVM:         %[[VA:.*]] = alloca i32
// LLVM:         store volatile i32 {{.*}}, ptr %[[VA]]
// LLVM:         load volatile i32, ptr %[[VA]]
// LLVM:         store volatile i32 {{.*}}, ptr %[[VA]]
// LLVM:         load volatile i32, ptr %[[VA]]
