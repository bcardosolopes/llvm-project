// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll --check-prefix=OGCG %s

// Atomic compound assignment slow path (CAS loop): operations that can't use
// atomicrmw (*, /, %, <<, >>, and bool/float types).

// Bool compound assignment requires CAS because atomicrmw doesn't support bool.
typedef _Atomic(_Bool) AtomicBool;
void test_bool_add(AtomicBool *b) {
  // CIR-LABEL: @test_bool_add
  // CIR: %[[INIT:.*]] = cir.load align(1) atomic(seq_cst)
  // CIR: cir.do {
  // CIR:   %[[EXPECTED:.*]] = cir.load align(1) %{{.*}} : !cir.ptr<!cir.bool>, !cir.bool
  // CIR:   %[[CONV:.*]] = cir.cast bool_to_int %[[EXPECTED]] : !cir.bool -> !s32i
  // CIR:   %[[ADD:.*]] = cir.binop(add, %[[CONV]], %{{.*}}) nsw : !s32i
  // CIR:   %[[RESULT:.*]] = cir.cast int_to_bool %[[ADD]] : !s32i -> !cir.bool
  // CIR:   %old, %success = cir.atomic.cmpxchg success(seq_cst) failure(seq_cst)
  // CIR-SAME: (!cir.ptr<!cir.bool>, !cir.bool, !cir.bool) -> (!cir.bool, !cir.bool)
  // CIR:   cir.yield
  // CIR: } while {
  // CIR:   cir.condition
  // CIR: }
  //
  // LLVM-LABEL: @test_bool_add
  // LLVM: %[[INIT:.*]] = load atomic i8, ptr %{{.*}} seq_cst, align 1
  // LLVM: cmpxchg ptr %{{.*}}, i8 %{{.*}}, i8 %{{.*}} seq_cst seq_cst, align 1
  //
  // OGCG-LABEL: @test_bool_add
  // OGCG: load atomic i8, ptr %{{.*}} seq_cst, align 1
  // OGCG: cmpxchg ptr %{{.*}}, i8 %{{.*}}, i8 %{{.*}} seq_cst seq_cst, align 1
  *b += 42;
}

// Float compound mul requires CAS because atomicrmw doesn't support fmul.
void test_float_mul(_Atomic(float) *f) {
  // CIR-LABEL: @test_float_mul
  // CIR: cir.do {
  // CIR:   %[[EXPECTED:.*]] = cir.load align(4) %{{.*}} : !cir.ptr<!cir.float>, !cir.float
  // CIR:   %[[MUL:.*]] = cir.binop(mul, %[[EXPECTED]], %{{.*}}) : !cir.float
  // CIR:   %old, %success = cir.atomic.cmpxchg success(seq_cst) failure(seq_cst)
  // CIR-SAME: (!cir.ptr<!cir.float>, !cir.float, !cir.float) -> (!cir.float, !cir.bool)
  // CIR:   cir.yield
  // CIR: } while {
  // CIR:   cir.condition
  // CIR: }
  //
  // LLVM-LABEL: @test_float_mul
  // LLVM: %[[BCAST_EXP:.*]] = bitcast float %{{.*}} to i32
  // LLVM: %[[BCAST_DES:.*]] = bitcast float %{{.*}} to i32
  // LLVM: cmpxchg ptr %{{.*}}, i32 %[[BCAST_EXP]], i32 %[[BCAST_DES]] seq_cst seq_cst, align 4
  // LLVM: bitcast i32 %{{.*}} to float
  //
  // OGCG-LABEL: @test_float_mul
  // OGCG: bitcast float %{{.*}} to i32
  // OGCG: cmpxchg ptr %{{.*}}, i32 %{{.*}}, i32 %{{.*}} seq_cst seq_cst, align 4
  // OGCG: bitcast i32 %{{.*}} to float
  *f *= 2.0f;
}

// Integer mul requires CAS because atomicrmw doesn't support mul.
void test_int_mul(_Atomic(int) *p) {
  // CIR-LABEL: @test_int_mul
  // CIR: cir.do {
  // CIR:   %[[EXPECTED:.*]] = cir.load align(4) %{{.*}} : !cir.ptr<!s32i>, !s32i
  // CIR:   %[[MUL:.*]] = cir.binop(mul, %[[EXPECTED]], %{{.*}}) nsw : !s32i
  // CIR:   %old, %success = cir.atomic.cmpxchg success(seq_cst) failure(seq_cst)
  // CIR-SAME: (!cir.ptr<!s32i>, !s32i, !s32i) -> (!s32i, !cir.bool)
  // CIR:   cir.yield
  // CIR: } while {
  // CIR:   cir.condition
  // CIR: }
  //
  // LLVM-LABEL: @test_int_mul
  // LLVM: cmpxchg ptr %{{.*}}, i32 %{{.*}}, i32 %{{.*}} seq_cst seq_cst, align 4
  //
  // OGCG-LABEL: @test_int_mul
  // OGCG: cmpxchg ptr %{{.*}}, i32 %{{.*}}, i32 %{{.*}} seq_cst seq_cst, align 4
  *p *= 3;
}

// Integer shift-left requires CAS because atomicrmw doesn't support shl.
void test_int_shl(_Atomic(int) *p) {
  // CIR-LABEL: @test_int_shl
  // CIR: cir.do {
  // CIR:   %[[EXPECTED:.*]] = cir.load align(4) %{{.*}} : !cir.ptr<!s32i>, !s32i
  // CIR:   cir.shift(left, %[[EXPECTED]] : !s32i, %{{.*}} : !s32i) -> !s32i
  // CIR:   %old, %success = cir.atomic.cmpxchg success(seq_cst) failure(seq_cst)
  // CIR-SAME: (!cir.ptr<!s32i>, !s32i, !s32i) -> (!s32i, !cir.bool)
  // CIR:   cir.yield
  // CIR: } while {
  // CIR:   cir.condition
  // CIR: }
  //
  // LLVM-LABEL: @test_int_shl
  // LLVM: cmpxchg ptr %{{.*}}, i32 %{{.*}}, i32 %{{.*}} seq_cst seq_cst, align 4
  *p <<= 2;
}

// Integer shift-right also requires CAS.
void test_int_shr(_Atomic(int) *p) {
  // CIR-LABEL: @test_int_shr
  // CIR: cir.do {
  // CIR:   cir.shift(right, %{{.*}} : !s32i, %{{.*}} : !s32i) -> !s32i
  // CIR:   %old, %success = cir.atomic.cmpxchg success(seq_cst) failure(seq_cst)
  // CIR-SAME: (!cir.ptr<!s32i>, !s32i, !s32i) -> (!s32i, !cir.bool)
  // CIR:   cir.yield
  // CIR: } while {
  // CIR:   cir.condition
  // CIR: }
  //
  // LLVM-LABEL: @test_int_shr
  // LLVM: cmpxchg ptr %{{.*}}, i32 %{{.*}}, i32 %{{.*}} seq_cst seq_cst, align 4
  *p >>= 2;
}
