// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that data member pointers to [[no_unique_address]] empty fields
// compile without crashing and produce the correct byte offsets. Empty
// [[no_unique_address]] fields have no storage in the CIR record type
// and thus no entry in fieldIdxMap — the member pointer must use byte
// offsets instead.

struct Empty {};

struct S {
  [[no_unique_address]] Empty e1;
  int a;
  [[no_unique_address]] Empty e2;
};

// e1 is at byte offset 0 (overlaps with a)
// CHECK: @p1 = global i64 0
// OGCG: @p1 = global i64 0
Empty S::*p1 = &S::e1;

// e2 is at byte offset 4 (after a)
// CHECK: @p2 = global i64 4
// OGCG: @p2 = global i64 4
Empty S::*p2 = &S::e2;

// Normal (non-empty) field for comparison
// CHECK: @p3 = global i64 0
// OGCG: @p3 = global i64 0
int S::*p3 = &S::a;

// Single [[no_unique_address]] field
struct T {
  [[no_unique_address]] Empty e;
  int x;
};
// CHECK: @p4 = global i64 0
// OGCG: @p4 = global i64 0
Empty T::*p4 = &T::e;

// Different empty types
struct Empty2 {};
struct U {
  [[no_unique_address]] Empty e;
  int x;
  [[no_unique_address]] Empty2 e2;
};
// CHECK: @p5 = global i64 0
// OGCG: @p5 = global i64 0
Empty U::*p5 = &U::e;

// CHECK: @p6 = global i64 0
// OGCG: @p6 = global i64 0
Empty2 U::*p6 = &U::e2;

// Anonymous struct member pointer — the member is inside an anonymous struct
// but the member pointer type refers to the outer class.
struct V {
  struct { int x; };
  int y;
};
// CHECK: @p7 = global i64 0
// OGCG: @p7 = global i64 0
int V::*p7 = &V::x;

// CHECK: @p8 = global i64 4
// OGCG: @p8 = global i64 4
int V::*p8 = &V::y;

// Non-constant member pointer (tests emitMemberPointerConstant in CIRGenModule)
void take_memptr(Empty S::*);
// CHECK-LABEL: @_Z18nonconstant_memptrv
// OGCG-LABEL: @_Z18nonconstant_memptrv
void nonconstant_memptr() {
  // CHECK: call void @_Z11take_memptrM1S5Empty(i64 0)
  // OGCG: call void @_Z11take_memptrM1S5Empty(i64 0)
  take_memptr(&S::e1);
  // CHECK: call void @_Z11take_memptrM1S5Empty(i64 4)
  // OGCG: call void @_Z11take_memptrM1S5Empty(i64 4)
  take_memptr(&S::e2);
}
