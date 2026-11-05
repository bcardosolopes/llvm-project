// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

volatile int volatile_x;
int plain_v;

int plain_x;
volatile int volatile_v;

volatile int volatile_write_x;

void atomic_read_volatile_x(void) {
#pragma omp atomic read
  plain_v = volatile_x;
}

// CHECK-LABEL: define {{.*}} @atomic_read_volatile_x(
// CHECK:         %[[VAL:.*]] = load atomic volatile i32, ptr @volatile_x monotonic, align 4
// CHECK-NEXT:    store i32 %[[VAL]], ptr @plain_v, align 4

// OGCG-LABEL: define {{.*}} @atomic_read_volatile_x(
// OGCG:         %[[VAL:.*]] = load atomic volatile i32, ptr @volatile_x monotonic, align 4
// OGCG-NEXT:    store i32 %[[VAL]], ptr @plain_v, align 4

void atomic_read_volatile_v(void) {
#pragma omp atomic read
  volatile_v = plain_x;
}

// CHECK-LABEL: define {{.*}} @atomic_read_volatile_v(
// CHECK:         %[[VAL:.*]] = load atomic i32, ptr @plain_x monotonic, align 4
// CHECK-NEXT:    store volatile i32 %[[VAL]], ptr @volatile_v, align 4

// OGCG-LABEL: define {{.*}} @atomic_read_volatile_v(
// OGCG:         %[[VAL:.*]] = load atomic i32, ptr @plain_x monotonic, align 4
// OGCG-NEXT:    store volatile i32 %[[VAL]], ptr @volatile_v, align 4

void atomic_write_volatile_x(void) {
#pragma omp atomic write
  volatile_write_x = 1;
}

// CHECK-LABEL: define {{.*}} @atomic_write_volatile_x(
// CHECK:         store atomic volatile i32 1, ptr @volatile_write_x monotonic, align 4

// OGCG-LABEL: define {{.*}} @atomic_write_volatile_x(
// OGCG:         store atomic volatile i32 1, ptr @volatile_write_x monotonic, align 4
