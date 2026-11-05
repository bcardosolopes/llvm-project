// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

int init();

int f() {
  static int x = init();
  return x;
}

// ARM guard-var ABI should use size_t-width guard vars.
// CIR: cir.global "private" internal dso_local @_ZGVZ1fvE1x = #cir.int<0> : !u64i
// LLVM: @_ZGVZ1fvE1x = internal global i64 0
// OGCG: @_ZGVZ1fvE1x = internal global i64 0

// ARM guard-var ABI tests only bit 0 of first byte in fast path.
// CIR: %[[B:.*]] = cir.load{{.*}} : !cir.ptr<!u8i>, !u8i
// CIR: %[[Z:.*]] = cir.const #cir.int<0> : !u8i
// CIR: %[[O:.*]] = cir.const #cir.int<1> : !u8i
// CIR: %[[M:.*]] = cir.binop(and, %[[B]], %[[O]]) : !u8i
// CIR: %[[C:.*]] = cir.cmp(eq, %[[M]], %[[Z]]) : !u8i, !cir.bool

// LLVM: %[[B:.*]] = load atomic i8, ptr @_ZGVZ1fvE1x acquire, align 8
// LLVM: %[[M:.*]] = and i8 %[[B]], 1
// LLVM: %[[C:.*]] = icmp eq i8 %[[M]], 0

// OGCG: %[[B:.*]] = load atomic i8, ptr @_ZGVZ1fvE1x acquire, align 8
// OGCG: %[[M:.*]] = and i8 %[[B]], 1
// OGCG: %[[C:.*]] = icmp eq i8 %[[M]], 0
