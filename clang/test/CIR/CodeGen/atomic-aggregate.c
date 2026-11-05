// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test atomic operations on aggregate types.
// Verifies that _Atomic struct assignment uses atomic store (emitAtomicStore)
// and _Atomic struct initialization uses atomic init (emitAtomicInit).

typedef struct { int x; int y; } Point;

// Aggregate assignment to _Atomic struct: evaluates RHS into a temp,
// then does an atomic store to the destination.
//
// CIR-LABEL: @test_atomic_aggregate_assign
// CIR: %[[LHS:.*]] = cir.load {{.*}} !cir.ptr<!rec_Point>
// CIR: cir.copy
// CIR: cir.load align(8) %{{.*}} : !cir.ptr<!u64i>, !u64i
// CIR: cir.store align(8) atomic(seq_cst) %{{.*}}, %{{.*}} : !u64i, !cir.ptr<!u64i>
//
// LLVM-LABEL: @test_atomic_aggregate_assign
// LLVM: store atomic i64 %{{.*}}, ptr %{{.*}} seq_cst, align 8
void test_atomic_aggregate_assign(_Atomic Point *dst, Point src) {
  *dst = src;
}

// Aggregate initialization of _Atomic struct variable.
//
// CIR-LABEL: @test_atomic_aggregate_init
// CIR: cir.copy %{{.*}} to %{{.*}}
//
// LLVM-LABEL: @test_atomic_aggregate_init
// LLVM: call void @llvm.memcpy
void test_atomic_aggregate_init(Point val) {
  _Atomic Point p = val;
}
