// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -debug-info-kind=limited -emit-llvm %s -o - | FileCheck %s

// A pointer to an incomplete enum (never defined in this TU) used to crash CIR's
// debug-info lowering: it called getTypeSize() on the incomplete enum type,
// which asserts (null integer type). It must instead emit a forward-declared
// enum DIType (size 0, DIFlagFwdDecl), matching classic CodeGen.

enum foo;
void test(enum foo *X) { (void)X; }

// CHECK: !DICompositeType(tag: DW_TAG_enumeration_type, name: "foo"
// CHECK-SAME: flags: DIFlagFwdDecl
