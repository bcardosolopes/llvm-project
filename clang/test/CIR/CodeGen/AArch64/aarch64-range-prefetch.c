// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +v8.9a -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +v8.9a -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

void test_range_prefetch(const void *addr) {
  __builtin_arm_range_prefetch(addr, 0, 0, 42);
}

// CIR: define {{.*}}void @test_range_prefetch(ptr {{.*}}%{{.*}})
// CIR:   call void @llvm.aarch64.range.prefetch(ptr %{{.*}}, i32 0, i32 0, i64 42)

// OGCG: define {{.*}}void @test_range_prefetch(ptr {{.*}}%{{.*}})
// OGCG:   call void @llvm.aarch64.range.prefetch(ptr %{{.*}}, i32 0, i32 0, i64 42)

void test_range_prefetch_x(const void *addr) {
  __builtin_arm_range_prefetch_x(addr, 1, 1, 64, 10, 128, 65536);
}

// The metadata packing for range_prefetch_x with args:
//   length=64, count=10-1=9, stride=128, distance=65536
//   distance: Log2_32_Ceil(65536) = 16, 30-16 = 14
//   metadata = (14 << 60) | (128 << 38) | (9 << 22) | 64
//            = 0xE000200002400040
// CIR: define {{.*}}void @test_range_prefetch_x(ptr {{.*}}%{{.*}})
// CIR:   call void @llvm.aarch64.range.prefetch(ptr %{{.*}}, i32 1, i32 1, i64 -2305807824803856320)

// OGCG: define {{.*}}void @test_range_prefetch_x(ptr {{.*}}%{{.*}})
// OGCG:   call void @llvm.aarch64.range.prefetch(ptr %{{.*}}, i32 1, i32 1, i64 -2305807824803856320)
