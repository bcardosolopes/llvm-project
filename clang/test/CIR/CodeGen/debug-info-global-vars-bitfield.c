// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

struct B {
  unsigned a : 3;
  unsigned b : 5;
};

struct B gb = {1, 2};

// CIR-DAG: !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
// CIR-DAG: !DIDerivedType(tag: DW_TAG_member, name: "a", {{.*}}size: 3, flags: DIFlagBitField, extraData: i64 0)
// CIR-DAG: !DIDerivedType(tag: DW_TAG_member, name: "b", {{.*}}size: 5, offset: 3, flags: DIFlagBitField, extraData: i64 0)

// OGCG-DAG: !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "a", {{.*}}size: 3, flags: DIFlagBitField, extraData: i64 0)
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_member, name: "b", {{.*}}size: 5, offset: 3, flags: DIFlagBitField, extraData: i64 0)
