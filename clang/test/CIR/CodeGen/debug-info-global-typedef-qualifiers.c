// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=NODUP

// Regression coverage: preserve typedef qualifier wrappers in
// global debug info metadata instead of dropping cv/restrict information.

typedef const int cint;
typedef volatile int vint;
typedef int *restrict rpint;
typedef const int *cpint;
typedef volatile int *vpint;

int x;
cint gc = 7;
vint gv = 8;
rpint grp = &x;
cpint gcp = &x;
vpint gvp = &x;

// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "cint"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "vint"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "rpint"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "cpint"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_typedef, name: "vpint"
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_const_type
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_volatile_type
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_restrict_type
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_pointer_type

// NODUP: !DIDerivedType(tag: DW_TAG_typedef, name: "cint"
// NODUP-NOT: DW_TAG_const_type, scope:
