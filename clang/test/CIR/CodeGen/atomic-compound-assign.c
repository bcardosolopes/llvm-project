// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll --check-prefix=OGCG %s

// Atomic compound assignment fast path: +=, -=, &=, ^=, |=
// These use atomicrmw directly instead of a CAS loop.

_Atomic(int) compound_add(_Atomic(int) in) {
  // CIR-LABEL: @compound_add
  // CIR: cir.atomic.fetch add seq_cst fetch_first %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i) -> !s32i
  // CIR: cir.binop(add, %{{.*}}, %{{.*}}) : !s32i
  //
  // LLVM-LABEL: @compound_add
  // LLVM: %[[OLD:.*]] = atomicrmw add ptr %{{.*}}, i32 5 seq_cst
  // LLVM: %[[NEW:.*]] = add i32 %[[OLD]], 5
  //
  // OGCG-LABEL: @compound_add
  // OGCG: atomicrmw add ptr %{{.*}}, i32 5 seq_cst
  return (in += 5);
}

_Atomic(int) compound_sub(_Atomic(int) in) {
  // CIR-LABEL: @compound_sub
  // CIR: cir.atomic.fetch sub seq_cst fetch_first %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i) -> !s32i
  // CIR: cir.binop(sub, %{{.*}}, %{{.*}}) : !s32i
  //
  // LLVM-LABEL: @compound_sub
  // LLVM: %[[OLD:.*]] = atomicrmw sub ptr %{{.*}}, i32 5 seq_cst
  // LLVM: %[[NEW:.*]] = sub i32 %[[OLD]], 5
  return (in -= 5);
}

_Atomic(int) compound_xor(_Atomic(int) in) {
  // CIR-LABEL: @compound_xor
  // CIR: cir.atomic.fetch xor seq_cst fetch_first %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i) -> !s32i
  // CIR: cir.binop(xor, %{{.*}}, %{{.*}}) : !s32i
  //
  // LLVM-LABEL: @compound_xor
  // LLVM: %[[OLD:.*]] = atomicrmw xor ptr %{{.*}}, i32 5 seq_cst
  // LLVM: %[[NEW:.*]] = xor i32 %[[OLD]], 5
  return (in ^= 5);
}

_Atomic(int) compound_or(_Atomic(int) in) {
  // CIR-LABEL: @compound_or
  // CIR: cir.atomic.fetch or seq_cst fetch_first %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i) -> !s32i
  // CIR: cir.binop(or, %{{.*}}, %{{.*}}) : !s32i
  //
  // LLVM-LABEL: @compound_or
  // LLVM: %[[OLD:.*]] = atomicrmw or ptr %{{.*}}, i32 5 seq_cst
  // LLVM: %[[NEW:.*]] = or i32 %[[OLD]], 5
  return (in |= 5);
}

_Atomic(int) compound_and(_Atomic(int) in) {
  // CIR-LABEL: @compound_and
  // CIR: cir.atomic.fetch and seq_cst fetch_first %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i) -> !s32i
  // CIR: cir.binop(and, %{{.*}}, %{{.*}}) : !s32i
  //
  // LLVM-LABEL: @compound_and
  // LLVM: %[[OLD:.*]] = atomicrmw and ptr %{{.*}}, i32 5 seq_cst
  // LLVM: %[[NEW:.*]] = and i32 %[[OLD]], 5
  return (in &= 5);
}

// Slow path: CAS loop for multiply (no atomicrmw for mul).
// Matches OGCG which uses cmpxchg loop.
_Atomic(int) compound_mul(_Atomic(int) in) {
  // CIR-LABEL: @compound_mul
  // CIR: cir.do
  // CIR: cir.atomic.cmpxchg
  //
  // LLVM-LABEL: @compound_mul
  // LLVM: cmpxchg
  //
  // OGCG-LABEL: @compound_mul
  // OGCG: cmpxchg
  return (in *= 5);
}
