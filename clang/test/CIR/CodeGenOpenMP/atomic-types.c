// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
// REQUIRES: x86-registered-target
//
// Test atomic operations on types that need special handling:
// - _Bool: lowers to i1 which is not byte-sized; must widen to i8.
// - Pointers: opaque ptr has 0 getScalarSizeInBits(); must use load/store
//   atomic ptr directly.
// - long double (x86_fp80): 80 bits = 10 bytes, not a power of 2; must use
//   __atomic_load/__atomic_store libcalls.

_Bool bx;
_Bool bv;

void test_bool_read(void) {
  #pragma omp atomic read
  bv = bx;
}
// CHECK-LABEL: define {{.*}} @test_bool_read()
// CHECK:         %[[VAL:.*]] = load atomic i8, ptr @bx monotonic, align 1
// CHECK:         store i8 %[[VAL]], ptr @bv, align 1

void test_bool_write(void) {
  #pragma omp atomic write
  bx = 1;
}
// CHECK-LABEL: define {{.*}} @test_bool_write()
// CHECK:         store atomic i8 1, ptr @bx monotonic, align 1

int *px;
int *pv;
int *p2;

void test_ptr_read(void) {
  #pragma omp atomic read
  pv = px;
}
// CHECK-LABEL: define {{.*}} @test_ptr_read()
// CHECK:         %[[PVAL:.*]] = load atomic ptr, ptr @px monotonic, align 8
// CHECK:         store ptr %[[PVAL]], ptr @pv, align 8

void test_ptr_write(void) {
  #pragma omp atomic write
  px = p2;
}
// CHECK-LABEL: define {{.*}} @test_ptr_write()
// CHECK:         store atomic ptr %{{.*}}, ptr @px monotonic, align 8

long double ldx;
long double ldv;

void test_longdouble_read(void) {
  #pragma omp atomic read
  ldv = ldx;
}
// CHECK-LABEL: define {{.*}} @test_longdouble_read()
// CHECK:         call void @__atomic_load(i64 16, ptr @ldx, ptr %{{.*}}, i32 0)
// CHECK:         %[[LDV:.*]] = load x86_fp80, ptr %{{.*}}, align 16
// CHECK:         store x86_fp80 %[[LDV]], ptr @ldv, align 16

void test_longdouble_write(void) {
  #pragma omp atomic write
  ldx = 3.14L;
}
// CHECK-LABEL: define {{.*}} @test_longdouble_write()
// CHECK:         store x86_fp80 0xK4000C8F5C28F5C28F5C3, ptr %{{.*}}
// CHECK:         call void @__atomic_store(i64 16, ptr @ldx, ptr %{{.*}}, i32 0)

// OGCG-LABEL: define {{.*}} @test_bool_read()
// OGCG:         load atomic i8, ptr @bx monotonic, align 1
// OGCG:         store i8 %{{.*}}, ptr @bv, align 1

// OGCG-LABEL: define {{.*}} @test_bool_write()
// OGCG:         store atomic i8 1, ptr @bx monotonic, align 1

// OGCG-LABEL: define {{.*}} @test_ptr_read()
// OGCG:         load atomic ptr, ptr @px monotonic, align 8
// OGCG:         store ptr %{{.*}}, ptr @pv, align 8

// OGCG-LABEL: define {{.*}} @test_ptr_write()
// OGCG:         store atomic ptr %{{.*}}, ptr @px monotonic, align 8

// OGCG-LABEL: define {{.*}} @test_longdouble_read()
// OGCG:         call void @__atomic_load(i64 noundef 16, ptr noundef @ldx, ptr noundef %{{.*}}, i32 noundef 0)
// OGCG:         store x86_fp80 %{{.*}}, ptr @ldv, align 16

// OGCG-LABEL: define {{.*}} @test_longdouble_write()
// OGCG:         store x86_fp80 0xK4000C8F5C28F5C28F5C3, ptr %{{.*}}
// OGCG:         call void @__atomic_store(i64 noundef 16, ptr noundef @ldx, ptr noundef %{{.*}}, i32 noundef 0)
