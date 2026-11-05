// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=OGCG

// Unnamed bitfields -- padding (`int :5;`) and zero-width alignment separators
// (`int :0;`) -- are omitted from record debug info by classic CodeGen. CIR
// previously rebuilt a spurious anonymous DW_TAG_member for each, which survived
// to the final DWARF as a nameless member with DW_AT_bit_size. Only the named
// members a, b, c may appear; no anonymous DW_TAG_member.

struct S {
  int a : 3;
  int : 5;
  int b : 4;
  int : 0;
  int c : 7;
};
struct S gs;

// CHECK: !DICompositeType(tag: DW_TAG_structure_type, name: "S"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "a"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "b"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member, name: "c"
// An unnamed bitfield member would print "DW_TAG_member, scope:" (no name:).
// CHECK-NOT: !DIDerivedType(tag: DW_TAG_member, scope:

// OGCG: !DICompositeType(tag: DW_TAG_structure_type, name: "S"
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "a"
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "b"
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "c"
// OGCG-NOT: !DIDerivedType(tag: DW_TAG_member, scope:
