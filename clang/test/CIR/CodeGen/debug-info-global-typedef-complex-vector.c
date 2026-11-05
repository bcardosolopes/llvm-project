// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=SUBRANGE --implicit-check-not='lowerBound: 0'

// Regression coverage: preserve complex/vector typedef base type
// metadata for globals.

typedef _Complex float CF;
typedef int V4 __attribute__((vector_size(16)));

CF gcf;
V4 gv;

// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "CF"
// CHECK-DAG: !DIBasicType(name: "complex", size: 64, encoding: DW_ATE_complex_float)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "V4"
// CHECK-DAG: !DICompositeType(tag: DW_TAG_array_type{{.*}}baseType: !{{[0-9]+}}, size: 128, flags: DIFlagVector

// SUBRANGE: !DISubrange(count: 4)
