// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
// REQUIRES: x86-registered-target
//
// Test atomic update of _Bool variables. LLVM requires atomic operations to be
// byte-sized, so _Bool (i1) must be widened to i8 for the omp.atomic.update
// pointer and block argument.

_Bool bx;
int iv;

void test_bool_update(void) {
#pragma omp atomic update
  bx ^= 1;
}

// CIR-LABEL: cir.func {{.*}} @test_bool_update()
// CIR:         %[[PTR:.*]] = cir.get_global @bx : !cir.ptr<!cir.bool>
// CIR:         %[[CAST:.*]] = cir.cast bitcast %[[PTR]] : !cir.ptr<!cir.bool> -> !cir.ptr<!u8i>
// CIR:         omp.atomic.update {{.*}} %[[CAST]] : !cir.ptr<!u8i>
// CIR:         ^bb0(%[[ARG:.*]]: !u8i
// CIR:           cir.cast int_to_bool %[[ARG]] : !u8i -> !cir.bool
// CIR:           cir.binop(xor,
// CIR:           cir.cast bool_to_int {{.*}} -> !u8i
// CIR:           omp.yield

// LLVM-LABEL: define {{.*}} @test_bool_update()
// LLVM:         load atomic i8, ptr @bx monotonic, align 1
// LLVM:         cmpxchg ptr @bx, i8 %{{.*}}, i8 %{{.*}} monotonic monotonic, align 1

// OGCG-LABEL: define {{.*}} @test_bool_update()
// OGCG:         atomicrmw xor ptr @bx, i8 1 monotonic, align 1

void test_bool_capture(void) {
  _Bool bv;
#pragma omp atomic capture
  { bv = bx; bx ^= 1; }
  iv = bv;
}

// CIR-LABEL: cir.func {{.*}} @test_bool_capture()
// CIR:         omp.atomic.capture
// CIR:           omp.atomic.read {{.*}} !u8i
// CIR:           omp.atomic.update {{.*}} !cir.ptr<!u8i>
// CIR:           ^bb0(%[[ARG2:.*]]: !u8i
// CIR:             cir.cast int_to_bool %[[ARG2]] : !u8i -> !cir.bool
// CIR:             omp.yield

// LLVM-LABEL: define {{.*}} @test_bool_capture()
// LLVM:         load atomic i8, ptr @bx monotonic, align 1
// LLVM:         cmpxchg ptr @bx, i8 %{{.*}}, i8 %{{.*}} monotonic monotonic, align 1

// OGCG-LABEL: define {{.*}} @test_bool_capture()
// OGCG:         atomicrmw xor ptr @bx, i8 1 monotonic, align 1
