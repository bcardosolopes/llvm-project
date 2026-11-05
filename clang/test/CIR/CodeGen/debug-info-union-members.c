// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -debug-info-kind=limited -emit-llvm %s -o - | FileCheck %s

// All union members must appear in DWARF. CIR's debug-info lowering matched AST
// fields to CIR record members by offset; since every union member is at offset
// 0, only the first field matched and the rest were silently dropped. A
// debugger could then interpret only one alternative of the union.

union U { int i; float f; char c[4]; double d; };
int main(void) { union U u; u.i = 7; return u.i; }

// CHECK: !DICompositeType(tag: DW_TAG_union_type, name: "U"{{.*}}elements: ![[ELEMS:[0-9]+]]
// CHECK: ![[ELEMS]] = !{![[MI:[0-9]+]], ![[MF:[0-9]+]], ![[MC:[0-9]+]], ![[MD:[0-9]+]]}
// CHECK-DAG: ![[MI]] = !DIDerivedType(tag: DW_TAG_member, name: "i"
// CHECK-DAG: ![[MF]] = !DIDerivedType(tag: DW_TAG_member, name: "f"
// CHECK-DAG: ![[MC]] = !DIDerivedType(tag: DW_TAG_member, name: "c"
// CHECK-DAG: ![[MD]] = !DIDerivedType(tag: DW_TAG_member, name: "d"
