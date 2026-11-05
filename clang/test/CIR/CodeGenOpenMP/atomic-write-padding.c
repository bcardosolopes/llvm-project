// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Verify that atomic store of types with padding (e.g. x86_fp80, which is 10
// bytes stored in a 16-byte slot) zeroes the padding bytes before passing the
// buffer to __atomic_store.  Without the memset, the 6 padding bytes would
// contain uninitialised stack data.

long double gld;

void atomic_write_long_double(long double val) {
#pragma omp atomic write
  gld = val;
}

// CHECK-LABEL: define {{.*}} @atomic_write_long_double(
// CHECK:         call void @llvm.memset.p0.i64(ptr align 16 %{{.*}}, i8 0, i64 16, i1 false)
// CHECK-NEXT:    store x86_fp80 %{{.*}}, ptr %{{.*}}, align 16
// CHECK-NEXT:    call void @__atomic_store(i64 16, ptr @gld, ptr %{{.*}}, i32 0)

// OGCG-LABEL: define {{.*}} @atomic_write_long_double(
// OGCG:         call void @llvm.memset.p0.i64(ptr align 16 %{{.*}}, i8 0, i64 16, i1 false)
// OGCG-NEXT:    store x86_fp80 %{{.*}}, ptr %{{.*}}, align 16
// OGCG-NEXT:    call void @__atomic_store(i64 noundef 16, ptr noundef @gld, ptr noundef %{{.*}}, i32 noundef 0)

// Also test atomic read of long double - the read path should work correctly
// (no padding zeroing needed for reads since we read INTO a buffer).
void atomic_read_long_double(long double *v) {
#pragma omp atomic read
  *v = gld;
}

// CHECK-LABEL: define {{.*}} @atomic_read_long_double(
// CHECK:         call void @__atomic_load(i64 16, ptr @gld, ptr %{{.*}}, i32 0)
// CHECK:         load x86_fp80

// OGCG-LABEL: define {{.*}} @atomic_read_long_double(
// OGCG:         call void @__atomic_load(i64 {{.*}}16, ptr {{.*}}@gld, ptr {{.*}}%{{.*}}, i32 {{.*}}0)
// OGCG:         load x86_fp80
