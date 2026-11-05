// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=OGCG

// An explicit member alignment (__attribute__((aligned)) / alignas) must survive
// on the record-debug reconstruction path used when a record is described only
// through a function (subprogram) rather than a module-level global. CIR rebuilds
// the DW_TAG_member DIDerivedType during lowering and previously dropped
// DW_AT_alignment on this path (the global-variable path was fixed separately);
// recompute it from the AST field (getMaxAlignment), matching classic CodeGen.

struct S {
  int a;
  int b __attribute__((aligned(16)));
};

int f(struct S *p) { return p->b; }

// The explicitly-aligned member carries align: 128 (16 bytes); the plain member
// has none (its line ends after size: 32).
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "a",{{.*}}size: 32)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "b",{{.*}}size: 32, align: 128, offset: 128)

// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "a",{{.*}}size: 32)
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "b",{{.*}}size: 32, align: 128, offset: 128)
