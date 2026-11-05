// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
// REQUIRES: x86-registered-target
//
// _Bool (which lowers to i1) is not byte-sized.  LLVM requires atomic
// loads/stores to be byte-sized, so CIR must widen to i8 for atomic ops.

_Bool bx;
_Bool bv;

void test_atomic_read(void) {
  #pragma omp atomic read
  bv = bx;
}
// CHECK-LABEL: define {{.*}} @test_atomic_read()
// CHECK:         %[[VAL:.*]] = load atomic i8, ptr @bx monotonic, align 1
// CHECK:         store i8 %[[VAL]], ptr @bv, align 1

void test_atomic_write(void) {
  #pragma omp atomic write
  bx = 1;
}
// CHECK-LABEL: define {{.*}} @test_atomic_write()
// CHECK:         store atomic i8 1, ptr @bx monotonic, align 1

// OGCG-LABEL: define {{.*}} @test_atomic_read()
// OGCG:         load atomic i8, ptr @bx monotonic, align 1
// OGCG:         store i8 %{{.*}}, ptr @bv, align 1

// OGCG-LABEL: define {{.*}} @test_atomic_write()
// OGCG:         store atomic i8 1, ptr @bx monotonic, align 1
