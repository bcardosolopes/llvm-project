// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s

// Regression coverage: preserve pointer chain shape under typedef
// debug metadata for global variables.

typedef int *PI;
typedef PI *PPI;
typedef int (*FN)(int);
typedef FN *PFN;
typedef int ARR4[4];
typedef ARR4 *PARR4;
typedef int V4 __attribute__((vector_size(16)));
typedef V4 *PV4;
struct S { int x; };
typedef struct S *PS;
typedef PS *PPS;

PPI gppi = 0;
PFN gpf = 0;
PARR4 gparr = 0;
PV4 gpv = 0;
PPS gps = 0;

// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "PPI"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "PFN"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "PARR4"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "ARR4"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "PV4"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "V4"
// CHECK-DAG: !DICompositeType(tag: DW_TAG_array_type{{.*}}flags: DIFlagVector
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "PPS"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "PS"
// CHECK-DAG: !DICompositeType(tag: DW_TAG_structure_type, name: "S",{{.*}}size: 32
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_member,{{.*}}baseType: !{{[0-9]+}}, size: 32
// CHECK-DAG: !DISubroutineType(
// CHECK-DAG: !DICompositeType(tag: DW_TAG_array_type{{.*}}size: 128
// CHECK-DAG: !DISubrange(count: 4)
// CHECK-DAG: !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !{{[0-9]+}}, size: 64)
