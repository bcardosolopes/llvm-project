// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +neon \
// RUN:   -emit-llvm -o - %s | FileCheck --check-prefix=OGCG %s

// REQUIRES: aarch64-registered-target

// Verify that single-member HFA/HVA record va_arg produces a direct memcpy
// from the va_list slot to the destination, rather than going through a typed
// load/store + intermediate alloca. This is especially important for packed
// structs where the struct alignment differs from the vector element alignment.

#include <arm_neon.h>
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_arg(v,l)   __builtin_va_arg(v,l)

// Single-member HVA with natural alignment (16).
struct single_vec { uint16x8_t v; };

void test_single_vec(int n, ...) {
  va_list vl;
  va_start(vl, n);
  struct single_vec s = va_arg(vl, struct single_vec);
  (void)s;

  // Both CIR and OGCG should emit a direct memcpy with align 16 / align 16.
  // CHECK-LABEL: define dso_local void @test_single_vec(
  // CHECK: phi ptr
  // CHECK-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr align 16 %{{.*}}, ptr align 16 %{{.*}}, i64 16, i1 false)
  // CHECK-NOT: load %struct.single_vec
  // CHECK: ret void
  // OGCG-LABEL: define dso_local void @test_single_vec(
  // OGCG: phi ptr
  // OGCG-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr align 16 %{{.*}}, ptr align 16 %{{.*}}, i64 16, i1 false)
  // OGCG: ret void
}

// Packed single-member HVA (alignment drops to 1).
struct __attribute__((packed)) packed_vec { uint16x8_t v; };

void test_packed_vec(int n, ...) {
  va_list vl;
  va_start(vl, n);
  struct packed_vec s = va_arg(vl, struct packed_vec);
  (void)s;

  // Direct memcpy with dst align 1, src align 8 (min of 16-byte reg, 8-byte stack).
  // CHECK-LABEL: define dso_local void @test_packed_vec(
  // CHECK: phi ptr
  // CHECK-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr align 1 %{{.*}}, ptr align 8 %{{.*}}, i64 16, i1 false)
  // CHECK-NOT: load %struct.packed_vec
  // CHECK: ret void
  // OGCG-LABEL: define dso_local void @test_packed_vec(
  // OGCG: phi ptr
  // OGCG-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr align 1 %{{.*}}, ptr align 8 %{{.*}}, i64 16, i1 false)
  // OGCG: ret void
}

// pragma pack(4) single-member HVA (alignment 4).
#pragma pack(4)
struct pragma4_vec { uint16x8_t v; };
#pragma pack()

void test_pragma4_vec(int n, ...) {
  va_list vl;
  va_start(vl, n);
  struct pragma4_vec s = va_arg(vl, struct pragma4_vec);
  (void)s;

  // Direct memcpy with dst align 4, src align 8.
  // CHECK-LABEL: define dso_local void @test_pragma4_vec(
  // CHECK: phi ptr
  // CHECK-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 8 %{{.*}}, i64 16, i1 false)
  // CHECK-NOT: load %struct.pragma4_vec
  // CHECK: ret void
  // OGCG-LABEL: define dso_local void @test_pragma4_vec(
  // OGCG: phi ptr
  // OGCG-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 8 %{{.*}}, i64 16, i1 false)
  // OGCG: ret void
}
