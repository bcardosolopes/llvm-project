// RUN: %clang_cc1 -triple aarch64-none-linux-android31 -fclangir -emit-cir %s -o - | FileCheck %s
//
// Regression test for stack overflow caused by DenseMap iterator
// invalidation in getCachedFields during isSafeToConvert.
// When isSafeToConvert recursively checks field types, calling
// getCachedFields for nested records can grow the DenseMap and
// invalidate ArrayRef pointers from earlier getCachedFields calls
// still on the call stack.
//
// This test creates a chain of record types that forces many
// getCachedFields calls during isSafeToConvert traversal, stressing
// the DenseMap growth path.

struct S0 { int x; };
struct S1 { struct S0 f; int y; };
struct S2 { struct S1 f; int y; };
struct S3 { struct S2 f; int y; };
struct S4 { struct S3 f; int y; };
struct S5 { struct S4 f; int y; };
struct S6 { struct S5 f; int y; };
struct S7 { struct S6 f; int y; };
struct S8 { struct S7 f; int y; };
struct S9 { struct S8 f; int y; };
struct S10 { struct S9 f; int y; };
struct S11 { struct S10 f; int y; };
struct S12 { struct S11 f; int y; };
struct S13 { struct S12 f; int y; };
struct S14 { struct S13 f; int y; };
struct S15 { struct S14 f; int y; };
struct S16 { struct S15 f; int y; };

// CHECK: cir.func {{.*}} @test
int test(struct S16 *p) {
  return p->f.f.f.f.f.f.f.f.f.f.f.f.f.f.f.f.x;
}
