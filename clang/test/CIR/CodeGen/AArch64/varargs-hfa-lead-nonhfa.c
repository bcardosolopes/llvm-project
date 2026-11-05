// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -emit-llvm -o - %s | FileCheck --check-prefix=OGCG %s

// REQUIRES: aarch64-registered-target

// An aggregate that LEADS with a run of >=2 homogeneous floating-point members
// but is NOT a pure HFA (it has trailing non-FP members) must be classified as
// a non-HFA and routed through the general-purpose register save area
// (gr_offs/gr_top), copied as one contiguous block. It must NOT be treated as
// an HFA (which would read elements at 16-byte VR-slot strides against the GP
// area, dropping the trailing member and reading past the aggregate).
//
// This guards the detectHFA/emitAAPCSVAArgBlocks fix: detectHFA leaked the
// partial member count from isHomogeneousAggregate's early failure, so
// emitAAPCSVAArgBlocks ran the HFA element-copy path for these non-HFAs.

typedef __builtin_va_list va_list;
#define va_arg(v,l) __builtin_va_arg(v,l)

extern void *the_list;

// --- leading 2-float run, non-HFA, 12 bytes: direct GP, single memcpy ---
struct lead2_small { float a, b; int c; };

struct lead2_small test_small(void) {
// CHECK-LABEL: define dso_local [2 x i64] @test_small
// OGCG-LABEL: define dso_local [2 x i64] @test_small
  return va_arg(*(va_list *)&the_list, struct lead2_small);

  // Uses gr_offs (offset 24), NEVER vr_offs (offset 28): this is not an HFA.
  // CHECK: load i32, ptr getelementptr inbounds nuw (i8, ptr @the_list, i64 24)
  // CHECK-NOT: getelementptr inbounds nuw (i8, ptr @the_list, i64 28)
  // OGCG: load i32, ptr getelementptr inbounds nuw (%struct.__va_list, ptr @the_list, i32 0, i32 3)

  // GP slot stride is the 8-aligned struct size (16), not a 2*16 VR HFA stride.
  // CHECK: add i32 %{{.*}}, 16
  // OGCG: add i32 %{{.*}}, 16

  // The whole struct is copied as one 12-byte block from the GP/stack area.
  // No per-element float loads at 16-byte VR-slot strides (the HFA path).
  // CHECK: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 8 %{{.*}}, i64 12
  // CHECK-NOT: load float, ptr %{{.*}}, align 16
  // OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 8 %{{.*}}, i64 12
}

// --- leading 2-double run, non-HFA, 24 bytes: indirect (must not assert) ---
struct lead2_big { double a, b; int c; };

struct lead2_big test_big(void) {
// CHECK-LABEL: define dso_local void @test_big
// OGCG-LABEL: define dso_local void @test_big
  return va_arg(*(va_list *)&the_list, struct lead2_big);

  // Non-HFA struct > 16 bytes is passed indirectly through the GP area. Before
  // the fix the leaked HFA count drove the (never-indirect) HFA element-copy
  // path and tripped an assert; it must now lower cleanly.
  // CHECK: load i32, ptr getelementptr inbounds nuw (i8, ptr @the_list, i64 24)
  // CHECK-NOT: getelementptr inbounds nuw (i8, ptr @the_list, i64 28)
  // OGCG: load i32, ptr getelementptr inbounds nuw (%struct.__va_list, ptr @the_list, i32 0, i32 3)

  // Indirect: the GP slot holds an 8-byte pointer that is dereferenced.
  // CHECK: add i32 %{{.*}}, 8
  // CHECK: load ptr, ptr %{{.*}}
  // CHECK: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %{{.*}}, ptr align 8 %{{.*}}, i64 24
  // OGCG: add i32 %{{.*}}, 8
  // OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %{{.*}}, ptr align 8 %{{.*}}, i64 24
}
