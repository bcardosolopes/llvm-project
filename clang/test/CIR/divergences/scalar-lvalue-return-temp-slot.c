// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
//
//
//
// Direct-CIR used to spill scalar lvalue returns through __retval even when
// the loaded SSA value could be returned directly.

struct S {
  int a;
};

int field_load(struct S *p) {
  return p->a;
}

int field_add(struct S *p, int value) {
  return p->a + value;
}

// CIR-LABEL: define dso_local i32 @field_load(
// CIR: [[PADDR:%.*]] = alloca ptr, i64 1, align 8
// CIR-NOT: alloca i32
// CIR: [[P:%.*]] = load ptr, ptr [[PADDR]], align 8
// CIR: [[A:%.*]] = getelementptr inbounds nuw %struct.S, ptr [[P]], i32 0, i32 0
// CIR: [[V:%.*]] = load i32, ptr [[A]], align 4
// CIR-NEXT: ret i32 [[V]]

// OGCG-LABEL: define dso_local i32 @field_load(
// OGCG: [[PADDR:%.*]] = alloca ptr, align 8
// OGCG-NOT: alloca i32
// OGCG: [[P:%.*]] = load ptr, ptr [[PADDR]], align 8
// OGCG: [[A:%.*]] = getelementptr inbounds nuw %struct.S, ptr [[P]], i32 0, i32 0
// OGCG: [[V:%.*]] = load i32, ptr [[A]], align 4
// OGCG-NEXT: ret i32 [[V]]

// CIR-LABEL: define dso_local i32 @field_add(
// CIR: [[PADDR:%.*]] = alloca ptr, i64 1, align 8
// CIR: [[VALUEADDR:%.*]] = alloca i32, i64 1, align 4
// CIR-NOT: alloca i32, i64 1, align 4
// CIR: [[P:%.*]] = load ptr, ptr [[PADDR]], align 8
// CIR: [[A:%.*]] = getelementptr inbounds nuw %struct.S, ptr [[P]], i32 0, i32 0
// CIR: [[FIELD:%.*]] = load i32, ptr [[A]], align 4
// CIR: [[VALUE:%.*]] = load i32, ptr [[VALUEADDR]], align 4
// CIR: [[ADD:%.*]] = add nsw i32 [[FIELD]], [[VALUE]]
// CIR-NEXT: ret i32 [[ADD]]

// OGCG-LABEL: define dso_local i32 @field_add(
// OGCG: [[PADDR:%.*]] = alloca ptr, align 8
// OGCG: [[VALUEADDR:%.*]] = alloca i32, align 4
// OGCG-NOT: alloca i32, align 4
// OGCG: [[P:%.*]] = load ptr, ptr [[PADDR]], align 8
// OGCG: [[A:%.*]] = getelementptr inbounds nuw %struct.S, ptr [[P]], i32 0, i32 0
// OGCG: [[FIELD:%.*]] = load i32, ptr [[A]], align 4
// OGCG: [[VALUE:%.*]] = load i32, ptr [[VALUEADDR]], align 4
// OGCG: [[ADD:%.*]] = add nsw i32 [[FIELD]], [[VALUE]]
// OGCG-NEXT: ret i32 [[ADD]]
