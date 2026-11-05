// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=OGCG

// An explicit alignment on a struct member or a typedef (__attribute__((aligned))
// / alignas) is recorded by classic CodeGen as DW_AT_alignment on the
// DW_TAG_member / DW_TAG_typedef DIDerivedType. CIR reconstructs these derived
// types during lowering but previously dropped the alignment; recompute it from
// the AST decl (getMaxAlignment) so the global-record member and the typedef both
// carry the same align as classic.

struct S {
  int a;
  int b __attribute__((aligned(16)));
};
struct S gs;

typedef int ai __attribute__((aligned(32)));
ai gai;

// The explicitly-aligned member carries align: 128 (16 bytes); the plain member
// has none.
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "b", scope: ![[#]], file: ![[#]], line: 13, baseType: ![[#]], size: 32, align: 128
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "ai", file: ![[#]], line: 17, baseType: ![[#]], align: 256

// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "b", scope: ![[#]], file: ![[#]], line: 13, baseType: ![[#]], size: 32, align: 128
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "ai", file: ![[#]], line: 17, baseType: ![[#]], align: 256
