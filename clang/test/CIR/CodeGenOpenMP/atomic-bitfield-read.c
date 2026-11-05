// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
// REQUIRES: x86-registered-target
//
// Test atomic read of bitfield members. The omp.atomic.read op doesn't
// understand bitfield extraction, so we bypass it: atomically load the
// entire storage integer, then extract the bitfield value.

struct S {
  int a : 8;
  int b : 24;
};

struct S s;
int v;

// Signed bitfield read.
void test_bitfield_read_signed(void) {
#pragma omp atomic read
  v = s.a;
}

// CIR-LABEL: cir.func {{.*}} @test_bitfield_read_signed()
// CIR:         cir.load {{.*}} atomic(relaxed) %{{.*}} : !cir.ptr<!u32i>, !u32i
// CIR:         cir.get_bitfield

// LLVM-LABEL: define {{.*}} @test_bitfield_read_signed()
// LLVM:         %[[LD:.*]] = load atomic i32, ptr @s monotonic, align 4
// LLVM:         store i32 %[[LD]], ptr %{{.*}}, align 4
// LLVM:         %[[BF:.*]] = load i32, ptr %{{.*}}, align 4
// LLVM:         %[[SHL:.*]] = shl i32 %[[BF]], 24
// LLVM:         %[[ASHR:.*]] = ashr i32 %[[SHL]], 24
// LLVM:         store i32 %[[ASHR]], ptr @v

// OGCG-LABEL: define {{.*}} @test_bitfield_read_signed()
// OGCG:         load atomic i32, ptr @s monotonic, align 4
// OGCG:         shl i32 %{{.*}}, 24
// OGCG:         ashr i32 %{{.*}}, 24
// OGCG:         store i32 %{{.*}}, ptr @v

// Unsigned bitfield read.
struct U {
  unsigned x : 16;
  unsigned y : 16;
};

struct U u;

void test_bitfield_read_unsigned(void) {
#pragma omp atomic read
  v = u.y;
}

// CIR-LABEL: cir.func {{.*}} @test_bitfield_read_unsigned()
// CIR:         cir.load {{.*}} atomic(relaxed) %{{.*}} : !cir.ptr<!u32i>, !u32i
// CIR:         cir.get_bitfield

// LLVM-LABEL: define {{.*}} @test_bitfield_read_unsigned()
// LLVM:         %[[LD2:.*]] = load atomic i32, ptr @u monotonic, align 4
// LLVM:         store i32 %[[LD2]], ptr %{{.*}}, align 4
// LLVM:         %[[BF2:.*]] = load i32, ptr %{{.*}}, align 4
// LLVM:         %[[LSHR:.*]] = lshr i32 %[[BF2]], 16
// LLVM:         store i32 %[[LSHR]], ptr @v

// OGCG-LABEL: define {{.*}} @test_bitfield_read_unsigned()
// OGCG:         load atomic i32, ptr @u monotonic, align 4
// OGCG:         lshr i32 %{{.*}}, 16
// OGCG:         store i32 %{{.*}}, ptr @v

// seq_cst ordering.
void test_bitfield_read_seq_cst(void) {
#pragma omp atomic seq_cst read
  v = s.a;
}

// CIR-LABEL: cir.func {{.*}} @test_bitfield_read_seq_cst()
// CIR:         cir.load {{.*}} atomic(seq_cst)
// CIR:         cir.get_bitfield

// LLVM-LABEL: define {{.*}} @test_bitfield_read_seq_cst()
// LLVM:         load atomic i32, ptr @s seq_cst, align 4

// OGCG-LABEL: define {{.*}} @test_bitfield_read_seq_cst()
// OGCG:         load atomic i32, ptr @s seq_cst, align 4
