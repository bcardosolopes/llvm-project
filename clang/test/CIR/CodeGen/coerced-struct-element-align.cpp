// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 -w %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that multi-element coerced struct return stores use correct
// per-element alignment (not the base alloca alignment for all elements).

struct alignas(16) A16 { int x, y, z, w; };
A16 make16();

// CHECK-LABEL: define {{.*}} @_Z5use16v()
// CHECK:   call { i64, i64 } @_Z6make16v()
// CHECK:   store i64 {{.*}}, ptr {{.*}}, align 16
// CHECK:   store i64 {{.*}}, ptr {{.*}}, align 8
int use16() {
  A16 a = make16();
  return a.x + a.w;
}

// Also verify that non-aligned structs still get correct alignment.
struct S8 { int x, y; };
S8 make8();

// CHECK-LABEL: define {{.*}} @_Z4use8v()
// CHECK:   call i64 @_Z5make8v()
// CHECK:   store i64 {{.*}}, ptr {{.*}}, align 4
int use8() {
  S8 s = make8();
  return s.x + s.y;
}
