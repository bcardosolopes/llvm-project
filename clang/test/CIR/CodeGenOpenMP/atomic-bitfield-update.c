// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
// REQUIRES: x86-registered-target
//
// Test atomic write, update, and capture of bitfield members. These operations
// use omp.atomic.update on the storage integer, with bitfield extraction and
// insertion in the update region body.

struct S {
  int a : 10;
  int b : 20;
};

struct S gs;

// --- Write ---

void test_bitfield_write(int val) {
#pragma omp atomic write
  gs.a = val;
}

// CIR-LABEL: cir.func {{.*}} @test_bitfield_write(
// CIR:         omp.atomic.update {{.*}} %{{.*}} : !cir.ptr<!u32i>
// CIR:         ^bb0(%[[ARG:.*]]: !u32i
// CIR:           cir.set_bitfield
// CIR:           omp.yield

// LLVM-LABEL: define {{.*}} @test_bitfield_write(
// LLVM:         load atomic i32, ptr @gs monotonic, align 4
// LLVM:         cmpxchg ptr @gs, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic, align 4

// --- Update ---

void test_bitfield_update(void) {
#pragma omp atomic update
  gs.a += 1;
}

// CIR-LABEL: cir.func {{.*}} @test_bitfield_update()
// CIR:         omp.atomic.update {{.*}} %{{.*}} : !cir.ptr<!u32i>
// CIR:         ^bb0(%[[ARG:.*]]: !u32i
// CIR:           cir.get_bitfield
// CIR:           cir.binop(add,
// CIR:           cir.set_bitfield
// CIR:           omp.yield

// LLVM-LABEL: define {{.*}} @test_bitfield_update()
// LLVM:         load atomic i32, ptr @gs monotonic, align 4
// LLVM:         cmpxchg ptr @gs, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic, align 4

// --- Capture (postfix: v = x; x update) ---

void test_bitfield_capture_postfix(void) {
  int v;
#pragma omp atomic capture
  { v = gs.a; gs.a += 1; }
}

// CIR-LABEL: cir.func {{.*}} @test_bitfield_capture_postfix()
// CIR:         omp.atomic.capture
// CIR:           omp.atomic.read %{{.*}} = %{{.*}} : !cir.ptr<!u32i>, !cir.ptr<!u32i>, !u32i
// CIR:           omp.atomic.update %{{.*}} : !cir.ptr<!u32i>
// CIR:           ^bb0(%[[ARG2:.*]]: !u32i
// CIR:             cir.get_bitfield
// CIR:             cir.set_bitfield
// CIR:             omp.yield
// CIR:         cir.get_bitfield

// LLVM-LABEL: define {{.*}} @test_bitfield_capture_postfix()
// LLVM:         load atomic i32, ptr @gs monotonic, align 4
// LLVM:         cmpxchg ptr @gs, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic, align 4

// --- Capture (prefix: x update; v = x) ---

void test_bitfield_capture_prefix(void) {
  int v;
#pragma omp atomic capture
  { gs.a += 1; v = gs.a; }
}

// CIR-LABEL: cir.func {{.*}} @test_bitfield_capture_prefix()
// CIR:         omp.atomic.capture
// CIR:           omp.atomic.update %{{.*}} : !cir.ptr<!u32i>
// CIR:           ^bb0(%[[ARG3:.*]]: !u32i
// CIR:             cir.get_bitfield
// CIR:             cir.set_bitfield
// CIR:             omp.yield
// CIR:           omp.atomic.read %{{.*}} = %{{.*}} : !cir.ptr<!u32i>, !cir.ptr<!u32i>, !u32i
// CIR:         cir.get_bitfield

// LLVM-LABEL: define {{.*}} @test_bitfield_capture_prefix()
// LLVM:         load atomic i32, ptr @gs monotonic, align 4
// LLVM:         cmpxchg ptr @gs, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic, align 4

// --- Capture with write (v = x; x = expr) ---

void test_bitfield_capture_write(int val) {
  int v;
#pragma omp atomic capture
  { v = gs.a; gs.a = val; }
}

// CIR-LABEL: cir.func {{.*}} @test_bitfield_capture_write(
// CIR:         omp.atomic.capture
// CIR:           omp.atomic.read
// CIR:           omp.atomic.update %{{.*}} : !cir.ptr<!u32i>
// CIR:           ^bb0(%{{.*}}: !u32i
// CIR:             cir.set_bitfield
// CIR:             omp.yield
// CIR:         cir.get_bitfield

// LLVM-LABEL: define {{.*}} @test_bitfield_capture_write(
// LLVM:         load atomic i32, ptr @gs monotonic, align 4
// LLVM:         cmpxchg ptr @gs, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic, align 4

// OGCG-LABEL: define {{.*}} @test_bitfield_write(
// OGCG:         load atomic i32, ptr @gs monotonic, align 4
// OGCG:         cmpxchg ptr @gs, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic, align 4

// OGCG-LABEL: define {{.*}} @test_bitfield_update()
// OGCG:         load atomic i32, ptr @gs monotonic, align 4
// OGCG:         shl i32 %{{.*}}, 22
// OGCG:         ashr i32 %{{.*}}, 22
// OGCG:         add nsw i32 %{{.*}}, 1
// OGCG:         cmpxchg ptr @gs, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic, align 4

// OGCG-LABEL: define {{.*}} @test_bitfield_capture_postfix()
// OGCG:         load atomic i32, ptr @gs monotonic, align 4
// OGCG:         cmpxchg ptr @gs, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic, align 4

// OGCG-LABEL: define {{.*}} @test_bitfield_capture_prefix()
// OGCG:         load atomic i32, ptr @gs monotonic, align 4
// OGCG:         cmpxchg ptr @gs, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic, align 4

// OGCG-LABEL: define {{.*}} @test_bitfield_capture_write(
// OGCG:         load atomic i32, ptr @gs monotonic, align 4
// OGCG:         cmpxchg ptr @gs, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic, align 4
