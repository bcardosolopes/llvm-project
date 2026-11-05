// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// An _Atomic-qualified global must keep its DW_TAG_atomic_type wrapper in debug
// info. CIR strips the atomic sugar to the underlying value type, so the wrapper
// is rebuilt from a forwarded attribute. (The base-type definition is matched
// first so the back-reference binds to the right node regardless of emit order.)

_Atomic int gai;
_Atomic long gal;

// CIR-DAG: ![[I:[0-9]+]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CIR-DAG: !DIDerivedType(tag: DW_TAG_atomic_type, baseType: ![[I]])
// CIR-DAG: ![[L:[0-9]+]] = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
// CIR-DAG: !DIDerivedType(tag: DW_TAG_atomic_type, baseType: ![[L]])

// OGCG-DAG: ![[I:[0-9]+]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_atomic_type, baseType: ![[I]])
// OGCG-DAG: ![[L:[0-9]+]] = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_atomic_type, baseType: ![[L]])
