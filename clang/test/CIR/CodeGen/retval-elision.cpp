// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s
// Tests __retval slot elision: a function with a single store/load pair
// through the return slot returns the stored value directly (mirroring
// classic CodeGen's dominating-store elision).

// CHECK-LABEL: cir.func{{.*}}@_Z10single_retii
// CHECK-NOT: cir.alloca "__retval"
// CHECK: cir.return %{{.*}} : !s32i
int single_ret(int a, int b) { return a + b; }

// CHECK-LABEL: cir.func{{.*}}@_Z9multi_retiii
// CHECK: cir.alloca "__retval"
int multi_ret(int c, int a, int b) {
  if (c) return a;
  return b;
}

// CHECK-LABEL: cir.func{{.*}}@_Z9fwd_parami
// CHECK-NOT: cir.alloca "__retval"
// CHECK: cir.return %{{.*}} : !s32i
int fwd_param(int x) { return x; }

// Volatile-qualified returns keep the slot: the memory traffic is
// (potentially) observable.
// CHECK-LABEL: cir.func{{.*}}@_Z7vol_reti
// CHECK: cir.alloca "__retval"
volatile int vol_ret(int a) { return a; }

struct Big {
  int x[8];
};

// Aggregate sret return of a call result: callee constructs directly into
// the caller's sret slot, no intermediate copy.
// CHECK-LABEL: cir.func{{.*}}@_Z7agg_fwdv
// CHECK: cir.call @_Z8make_bigv(%arg0)
// CHECK-NEXT: cir.return
Big make_big();
Big agg_fwd() { return make_big(); }

// Wide bitints return sret on x86_64 SysV. Elision leaves a value-fed
// return, which CallConvLowering stores into the sret slot directly.
// CHECK-LABEL: cir.func{{.*}}@_Z6fwd200DB200_
// CHECK-NOT: cir.alloca "__retval"
// CHECK: cir.store %{{.*}}, %arg0 : !cir.int<s, 200, bitint>
// CHECK-NEXT: cir.return
_BitInt(200) fwd200(_BitInt(200) x) { return x; }

// CHECK-LABEL: cir.func{{.*}}@_Z8const200v
// CHECK-NOT: cir.alloca "__retval"
// CHECK: cir.store %{{.*}}, %arg0 : !cir.int<s, 200, bitint>
// CHECK-NEXT: cir.return
_BitInt(200) const200(void) { return 5; }
