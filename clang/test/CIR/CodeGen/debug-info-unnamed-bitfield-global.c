// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=OGCG

// Unnamed bitfields (the `int : 0;` alignment separator and `int : N;` padding)
// are omitted from a record's debug info by classic CodeGen. The subprogram
// path already skipped them, but the global-only record builder reconstructed
// every entry from the CIR field attributes and emitted a spurious anonymous
// zero-size DW_TAG_member for `int : 0`. Verify a record referenced only by a
// global variable carries exactly the two named bitfields and no phantom member.

struct BF {
  int a : 3;
  int : 0;
  int b : 5;
};

struct BF gbf;

// CHECK: !DICompositeType(tag: DW_TAG_structure_type, name: "BF",{{.*}}elements: ![[ELEMS:[0-9]+]]
// CHECK: ![[ELEMS]] = !{![[A:[0-9]+]], ![[B:[0-9]+]]}
// CHECK: ![[A]] = !DIDerivedType(tag: DW_TAG_member, name: "a",{{.*}}size: 3, flags: DIFlagBitField
// CHECK: ![[B]] = !DIDerivedType(tag: DW_TAG_member, name: "b",{{.*}}size: 5, offset: 32, flags: DIFlagBitField

// OGCG: !DICompositeType(tag: DW_TAG_structure_type, name: "BF",{{.*}}elements: ![[OELEMS:[0-9]+]]
// OGCG: ![[OELEMS]] = !{![[OA:[0-9]+]], ![[OB:[0-9]+]]}
// OGCG: ![[OA]] = !DIDerivedType(tag: DW_TAG_member, name: "a",{{.*}}size: 3, flags: DIFlagBitField
// OGCG: ![[OB]] = !DIDerivedType(tag: DW_TAG_member, name: "b",{{.*}}size: 5, offset: 32, flags: DIFlagBitField
