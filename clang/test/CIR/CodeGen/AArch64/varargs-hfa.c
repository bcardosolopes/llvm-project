// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -emit-llvm -o - %s | FileCheck --check-prefix=OGCG %s

// REQUIRES: aarch64-registered-target

// Test that AArch64 AAPCS va_arg properly routes Homogeneous Floating-point
// Aggregates (HFAs) through the FP/SIMD register save area (vr_offs/vr_top)
// instead of the general-purpose register save area (gr_offs/gr_top).
// HFA elements are stored 16 bytes apart in the VR save area and must be
// reconstructed into contiguous memory when there are multiple members.

typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v,l) __builtin_va_arg(v,l)

extern void *the_list;

// --- 2-member float HFA ---
struct hfa2 { float a, b; };

struct hfa2 test_hfa2(void) {
// CHECK-LABEL: define dso_local %struct.hfa2 @test_hfa2
// OGCG-LABEL: define dso_local %struct.hfa2 @test_hfa2
  return va_arg(*(va_list *)&the_list, struct hfa2);

  // Must use vr_offs (offset 28), not gr_offs (offset 24)
  // CHECK: load i32, ptr getelementptr inbounds nuw (i8, ptr @the_list, i64 28)
  // OGCG: load i32, ptr getelementptr inbounds nuw (%struct.__va_list, ptr @the_list, i32 0, i32 4)

  // Must add 32 (2 * 16) to vr_offs, not 8 to gr_offs
  // CHECK: add i32 %{{.*}}, 32
  // OGCG: add i32 %{{.*}}, 32

  // Must store updated offset back to vr_offs
  // CHECK: store i32 %{{.*}}, ptr getelementptr inbounds nuw (i8, ptr @the_list, i64 28)
  // OGCG: store i32 %{{.*}}, ptr getelementptr inbounds nuw (%struct.__va_list, ptr @the_list, i32 0, i32 4)

  // Must load from vr_top (offset 16), not gr_top (offset 8)
  // CHECK: load ptr, ptr getelementptr inbounds nuw (i8, ptr @the_list, i64 16)
  // OGCG: load ptr, ptr getelementptr inbounds nuw (%struct.__va_list, ptr @the_list, i32 0, i32 2)

  // HFA reconstruction: load elements at 16-byte intervals, store contiguously
  // CHECK: getelementptr inbounds i8, ptr %{{.*}}, {{.*}} 0
  // CHECK: load float, ptr %{{.*}}, align 16
  // CHECK: store float
  // CHECK: getelementptr inbounds i8, ptr %{{.*}}, {{.*}} 16
  // CHECK: load float, ptr %{{.*}}, align 16
  // CHECK: store float

  // OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 0
  // OGCG: load float, ptr %{{.*}}, align 16
  // OGCG: store float
  // OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 16
  // OGCG: load float, ptr %{{.*}}, align 16
  // OGCG: store float

  // On-stack: advance by 8 bytes (struct size 8, rounded up to 8)
  // CHECK: getelementptr inbounds i8, ptr %{{.*}}, i64 8
  // OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 8
}

// --- 4-member float HFA ---
struct hfa4 { float a, b, c, d; };

struct hfa4 test_hfa4(void) {
// CHECK-LABEL: define dso_local %struct.hfa4 @test_hfa4
// OGCG-LABEL: define dso_local %struct.hfa4 @test_hfa4
  return va_arg(*(va_list *)&the_list, struct hfa4);

  // Must add 64 (4 * 16) to vr_offs
  // CHECK: load i32, ptr getelementptr inbounds nuw (i8, ptr @the_list, i64 28)
  // CHECK: add i32 %{{.*}}, 64

  // OGCG: load i32, ptr getelementptr inbounds nuw (%struct.__va_list, ptr @the_list, i32 0, i32 4)
  // OGCG: add i32 %{{.*}}, 64

  // HFA reconstruction: 4 elements at 16-byte intervals
  // CHECK: getelementptr inbounds i8, ptr %{{.*}}, {{.*}} 0
  // CHECK: load float, ptr %{{.*}}, align 16
  // CHECK: getelementptr inbounds i8, ptr %{{.*}}, {{.*}} 16
  // CHECK: load float, ptr %{{.*}}, align 16
  // CHECK: getelementptr inbounds i8, ptr %{{.*}}, {{.*}} 32
  // CHECK: load float, ptr %{{.*}}, align 16
  // CHECK: getelementptr inbounds i8, ptr %{{.*}}, {{.*}} 48
  // CHECK: load float, ptr %{{.*}}, align 16

  // OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 0
  // OGCG: load float, ptr %{{.*}}, align 16
  // OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 16
  // OGCG: load float, ptr %{{.*}}, align 16
  // OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 32
  // OGCG: load float, ptr %{{.*}}, align 16
  // OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 48
  // OGCG: load float, ptr %{{.*}}, align 16

  // On-stack: advance by 16 bytes (struct size 16, rounded up to 8)
  // CHECK: getelementptr inbounds i8, ptr %{{.*}}, i64 16
  // OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 16
}

// --- 2-member double HFA ---
struct hfa_double { double x, y; };

struct hfa_double test_hfa_double(void) {
// CHECK-LABEL: define dso_local %struct.hfa_double @test_hfa_double
// OGCG-LABEL: define dso_local %struct.hfa_double @test_hfa_double
  return va_arg(*(va_list *)&the_list, struct hfa_double);

  // Must use vr_offs and add 32 (2 * 16)
  // CHECK: load i32, ptr getelementptr inbounds nuw (i8, ptr @the_list, i64 28)
  // CHECK: add i32 %{{.*}}, 32
  // OGCG: load i32, ptr getelementptr inbounds nuw (%struct.__va_list, ptr @the_list, i32 0, i32 4)
  // OGCG: add i32 %{{.*}}, 32

  // HFA reconstruction with doubles at 16-byte intervals
  // CHECK: load double, ptr %{{.*}}, align 16
  // CHECK: store double
  // CHECK: getelementptr inbounds i8, ptr %{{.*}}, {{.*}} 16
  // CHECK: load double, ptr %{{.*}}, align 16
  // CHECK: store double

  // OGCG: load double, ptr %{{.*}}, align 16
  // OGCG: store double
  // OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 16
  // OGCG: load double, ptr %{{.*}}, align 16
  // OGCG: store double
}

// --- 1-member float HFA (no reconstruction needed) ---
struct hfa1 { float a; };

struct hfa1 test_hfa1(void) {
// CHECK-LABEL: define dso_local %struct.hfa1 @test_hfa1
// OGCG-LABEL: define dso_local %struct.hfa1 @test_hfa1
  return va_arg(*(va_list *)&the_list, struct hfa1);

  // Still uses vr_offs, but only one 16-byte slot
  // CHECK: load i32, ptr getelementptr inbounds nuw (i8, ptr @the_list, i64 28)
  // CHECK: add i32 %{{.*}}, 16
  // OGCG: load i32, ptr getelementptr inbounds nuw (%struct.__va_list, ptr @the_list, i32 0, i32 4)
  // OGCG: add i32 %{{.*}}, 16
}

// --- 4-member vector HVA with padded short-vector elements ---
typedef __attribute__((ext_vector_type(3))) float float3;
struct hva_v3 { float3 arr[4]; };

struct hva_v3 test_hva_v3(void) {
// CHECK-LABEL: define dso_local %struct.hva_v3 @test_hva_v3
// OGCG-LABEL: define dso_local %struct.hva_v3 @test_hva_v3
  return va_arg(*(va_list *)&the_list, struct hva_v3);

  // The padded 16-byte vector slots still form an HVA, so use vr_offs.
  // CHECK: load i32, ptr getelementptr inbounds nuw (i8, ptr @the_list, i64 28)
  // CHECK-NOT: getelementptr inbounds nuw (i8, ptr @the_list, i64 24)
  // CHECK: add i32 %{{.*}}, 64

  // OGCG: load i32, ptr getelementptr inbounds nuw (%struct.__va_list, ptr @the_list, i32 0, i32 4)
  // OGCG-NOT: getelementptr inbounds nuw (%struct.__va_list, ptr @the_list, i32 0, i32 3)
  // OGCG: add i32 %{{.*}}, 64
}

// --- 5-member vector aggregate is too large to be an HVA ---
typedef __attribute__((ext_vector_type(4))) int int4;
struct too_big_hva { int4 a, b, c, d, e; };

struct too_big_hva test_too_big_hva(void) {
// CHECK-LABEL: define dso_local void @test_too_big_hva
// OGCG-LABEL: define dso_local void @test_too_big_hva
  return va_arg(*(va_list *)&the_list, struct too_big_hva);

  // Oversized HVAs must fall back to the indirect GR path instead of crashing.
  // CHECK: load i32, ptr getelementptr inbounds nuw (i8, ptr @the_list, i64 24)
  // CHECK: add i32 %{{.*}}, 8
  // OGCG: load i32, ptr getelementptr inbounds nuw (%struct.__va_list, ptr @the_list, i32 0, i32 3)
  // OGCG: add i32 %{{.*}}, 8
}

// --- Non-HFA struct (should still use gr_offs) ---
struct non_hfa { int x; float y; };

struct non_hfa test_non_hfa(void) {
// CHECK-LABEL: define dso_local i64 @test_non_hfa
// OGCG-LABEL: define dso_local i64 @test_non_hfa
  return va_arg(*(va_list *)&the_list, struct non_hfa);

  // Mixed int/float struct is NOT an HFA — uses gr_offs (offset 24)
  // CHECK: load i32, ptr getelementptr inbounds nuw (i8, ptr @the_list, i64 24)
  // OGCG: load i32, ptr getelementptr inbounds nuw (%struct.__va_list, ptr @the_list, i32 0, i32 3)
}
