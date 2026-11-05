// REQUIRES: x86-registered-target
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that multi-qualified types in function signatures preserve all
// qualifiers in debug info when routed through buildSourceStandaloneDIType.
// Previously, the function stripped all qualifiers at once via
// getLocalUnqualifiedType(), so typedef'd 'const volatile int' lost the
// volatile qualifier.

typedef const volatile int cv_int;

void f(cv_int *p) {
  (void)p;
}

// The typedef should resolve to const(volatile(int)) in debug info.

// CIR-DAG: ![[INT:[0-9]+]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CIR-DAG: ![[VOL:[0-9]+]] = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: ![[INT]])
// CIR-DAG: ![[CV:[0-9]+]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[VOL]])
// CIR-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "cv_int", {{.*}}baseType: ![[CV]])

// OGCG-DAG: ![[INT:[0-9]+]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// OGCG-DAG: ![[VOL:[0-9]+]] = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: ![[INT]])
// OGCG-DAG: ![[CV:[0-9]+]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[VOL]])
// OGCG-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "cv_int", {{.*}}baseType: ![[CV]])
