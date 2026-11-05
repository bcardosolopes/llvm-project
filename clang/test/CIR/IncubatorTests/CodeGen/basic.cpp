// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

int *p0() {
  int *p = nullptr;
  return p;
}

// CHECK: cir.func {{.*}} @_Z2p0v() -> (!cir.ptr<!s32i> {cir.noundef})
// CHECK: %[[P0_ADDR:.*]] = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["p", init]
// CHECK: %[[P0_NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!s32i>
// CHECK: cir.store{{.*}} %[[P0_NULL]], %[[P0_ADDR]] : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>

int *p1() {
  int *p;
  p = nullptr;
  return p;
}

// CHECK: cir.func {{.*}} @_Z2p1v() -> (!cir.ptr<!s32i> {cir.noundef})
// CHECK: %[[P1_ADDR:.*]] = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["p"]
// CHECK: %[[P1_NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!s32i>
// CHECK: cir.store{{.*}} %[[P1_NULL]], %[[P1_ADDR]] : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>

int *p2() {
  int *p = nullptr;
  {
    int x = 0;
    p = &x;
    *p = 42;
  }
  *p = 42;
  return p;
}

// CHECK: cir.func {{.*}} @_Z2p2v() -> (!cir.ptr<!s32i> {cir.noundef})
// CHECK-NEXT:  %[[P2_ADDR:.*]] = cir.alloca !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>, ["p", init] {alignment = 8 : i64}
// CHECK-NEXT:  %[[P2_NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!s32i>
// CHECK-NEXT:  cir.store{{.*}} %[[P2_NULL]], %[[P2_ADDR]] : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
// CHECK-NEXT:  cir.scope {
// CHECK-NEXT:    %[[X_ADDR:.*]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init] {alignment = 4 : i64}
// CHECK-NEXT:    %[[ZERO:.*]] = cir.const #cir.int<0> : !s32i
// CHECK-NEXT:    cir.store{{.*}} %[[ZERO]], %[[X_ADDR]] : !s32i, !cir.ptr<!s32i>
// CHECK-NEXT:    cir.store{{.*}} %[[X_ADDR]], %[[P2_ADDR]] : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>
// CHECK-NEXT:    %[[INNER_C42:.*]] = cir.const #cir.int<42> : !s32i
// CHECK-NEXT:    %[[INNER_P:.*]] = cir.load deref{{.*}}  %[[P2_ADDR]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// CHECK-NEXT:    cir.store{{.*}} %[[INNER_C42]], %[[INNER_P]] : !s32i, !cir.ptr<!s32i>
// CHECK-NEXT:  } loc(#[[locScope:loc[0-9]+]])
// CHECK-NEXT:  %[[OUTER_C42:.*]] = cir.const #cir.int<42> : !s32i
// CHECK-NEXT:  %[[OUTER_P:.*]] = cir.load deref{{.*}}  %[[P2_ADDR]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// CHECK-NEXT:  cir.store{{.*}} %[[OUTER_C42]], %[[OUTER_P]] : !s32i, !cir.ptr<!s32i>
// CHECK-NEXT:  %[[P2_RET:.*]] = cir.load{{.*}} %[[P2_ADDR]] : !cir.ptr<!cir.ptr<!s32i>>, !cir.ptr<!s32i>
// CHECK-NEXT:  cir.return %[[P2_RET]] : !cir.ptr<!s32i>

void b0() { bool x = true, y = false; }

// CHECK: cir.func {{.*}} @_Z2b0v()
// CHECK: %2 = cir.const #true
// CHECK: %3 = cir.const #false

void b1(int a) { bool b = a; }

// CHECK: cir.func {{.*}} @_Z2b1i(%arg0: !s32i {cir.noundef} loc({{.*}}))
// CHECK: %2 = cir.load{{.*}} %0 : !cir.ptr<!s32i>, !s32i
// CHECK: %3 = cir.cast int_to_bool %2 : !s32i -> !cir.bool
// CHECK: cir.store{{.*}} %3, %1 : !cir.bool, !cir.ptr<!cir.bool>

void if0(int a) {
  int x = 0;
  if (a) {
    x = 3;
  } else {
    x = 4;
  }
}

// CHECK: cir.func {{.*}} @_Z3if0i(%arg0: !s32i {cir.noundef} loc({{.*}}))
// CHECK:   %[[LOAD:.*]] = cir.load{{.*}} %0 : !cir.ptr<!s32i>, !s32i
// CHECK:   %[[CAST:.*]] = cir.cast int_to_bool %[[LOAD]] : !s32i -> !cir.bool
// CHECK-NEXT:   cir.if %[[CAST]] {
// CHECK-NEXT:     %[[C3:.*]] = cir.const #cir.int<3> : !s32i
// CHECK-NEXT:     cir.store{{.*}} %[[C3]], %1 : !s32i, !cir.ptr<!s32i>
// CHECK-NEXT:   } else {
// CHECK-NEXT:     %[[C4:.*]] = cir.const #cir.int<4> : !s32i
// CHECK-NEXT:     cir.store{{.*}} %[[C4]], %1 : !s32i, !cir.ptr<!s32i>
// CHECK-NEXT:   }

void if1(int a, bool b, bool c) {
  int x = 0;
  if (a) {
    x = 3;
    if (b) {
      x = 8;
    }
  } else {
    if (c) {
      x = 14;
    }
    x = 4;
  }
}

// CHECK: cir.func {{.*}} @_Z3if1ibb(%arg0: !s32i {cir.noundef} loc({{.*}}), %arg1: !cir.bool {cir.noundef, cir.zeroext} loc({{.*}}), %arg2: !cir.bool {cir.noundef, cir.zeroext} loc({{.*}}))
// CHECK:   %[[LOAD:.*]] = cir.load{{.*}} %0 : !cir.ptr<!s32i>, !s32i
// CHECK:   %[[CAST:.*]] = cir.cast int_to_bool %[[LOAD]] : !s32i -> !cir.bool
// CHECK:   cir.if %[[CAST]] {
// CHECK:     %[[C3:.*]] = cir.const #cir.int<3> : !s32i
// CHECK:     cir.store{{.*}} %[[C3]], %3 : !s32i, !cir.ptr<!s32i>
// CHECK:       %[[LOADB:.*]] = cir.load{{.*}} %1 : !cir.ptr<!cir.bool>, !cir.bool
// CHECK-NEXT:       cir.if %[[LOADB]] {
// CHECK-NEXT:         %[[C8:.*]] = cir.const #cir.int<8> : !s32i
// CHECK-NEXT:         cir.store{{.*}} %[[C8]], %3 : !s32i, !cir.ptr<!s32i>
// CHECK-NEXT:       }
// CHECK:   } else {
// CHECK:       %[[LOADC:.*]] = cir.load{{.*}} %2 : !cir.ptr<!cir.bool>, !cir.bool
// CHECK-NEXT:       cir.if %[[LOADC]] {
// CHECK-NEXT:         %[[C14:.*]] = cir.const #cir.int<14> : !s32i
// CHECK-NEXT:         cir.store{{.*}} %[[C14]], %3 : !s32i, !cir.ptr<!s32i>
// CHECK-NEXT:       }
// CHECK:     %[[C4:.*]] = cir.const #cir.int<4> : !s32i
// CHECK:     cir.store{{.*}} %[[C4]], %3 : !s32i, !cir.ptr<!s32i>
// CHECK:   }

enum {
  um = 0,
  dois = 1,
}; // Do not crash!

extern "C" {
struct regs {
  unsigned long sp;
  unsigned long pc;
};

// Check it's not mangled.
// CHECK: cir.func {{.*}} @use_regs()

void use_regs() { regs r; }
}

void x() {
  const bool b0 = true;
  const bool b1 = false;
}

// CHECK: cir.func {{.*}} @_Z1xv()
// CHECK:   %0 = cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["b0", init, const] {alignment = 1 : i64}
// CHECK:   %1 = cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["b1", init, const] {alignment = 1 : i64}
// CHECK:   %2 = cir.const #true
// CHECK:   cir.store{{.*}} %2, %0 : !cir.bool, !cir.ptr<!cir.bool>
// CHECK:   %3 = cir.const #false
// CHECK:   cir.store{{.*}} %3, %1 : !cir.bool, !cir.ptr<!cir.bool>

typedef unsigned long size_type;
typedef unsigned long _Tp;

size_type max_size() {
  return size_type(~0) / sizeof(_Tp);
}

// CHECK: cir.func {{.*}} @_Z8max_sizev()
// Upstream constant-folds size_type(~0) directly to UINT64_MAX
// CHECK:   %[[MAX:.*]] = cir.const #cir.int<18446744073709551615> : !u64i
// CHECK:   %[[SIZE:.*]] = cir.const #cir.int<8> : !u64i
// CHECK:   %[[QUOT:.*]] = cir.binop(div, %[[MAX]], %[[SIZE]]) : !u64i
// CHECK:   cir.return %[[QUOT]] : !u64i

// CHECK-DAG: #[[locScope]] = loc(fused[#[[locScopeA:loc[0-9]+]], #[[locScopeB:loc[0-9]+]]])
// CHECK-DAG: #[[locScopeA]] = loc("{{.*}}basic.cpp":27:3)
// CHECK-DAG: #[[locScopeB]] = loc("{{.*}}basic.cpp":31:3)
