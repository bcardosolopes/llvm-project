// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes -debug-info-kind=limited %s -o - | FileCheck %s --check-prefixes=COMMON,OGCG
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -emit-llvm -disable-llvm-passes -debug-info-kind=limited %s -o - | FileCheck %s --check-prefixes=COMMON,OGCG
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes -debug-info-kind=limited %s -o - | FileCheck %s --check-prefixes=COMMON,CIR %}
// RUN: %if cir-support %{ %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes -debug-info-kind=limited %s -o - | FileCheck %s --check-prefixes=COMMON,CIR %}

#define _(x) (__builtin_preserve_access_index(x))

struct s1 {
  char a;
  int b[4];
};

const void *unit1(struct s1 *arg) {
  return _(&arg->b[2]);
}
// COMMON-LABEL: define{{.*}} @unit1
// COMMON: call ptr @llvm.preserve.struct.access.index.p0.p0(ptr elementtype(%struct.s1) %{{[0-9a-z]+}}, i32 1, i32 1), !dbg [[UNIT1_STRUCT_LOC:![0-9]+]], !llvm.preserve.access.index ![[STRUCT_S1:[0-9]+]]
// COMMON: call ptr @llvm.preserve.array.access.index.p0.p0(ptr elementtype([4 x i32]) %{{[0-9a-z]+}}, i32 1, i32 2), !dbg [[UNIT1_ARRAY_LOC:![0-9]+]], !llvm.preserve.access.index ![[ARRAY:[0-9]+]]

union u1 {
  char a;
  int b;
};

const void *unit2(union u1 *arg) {
  return _(&arg->b);
}
// COMMON-LABEL: define{{.*}} @unit2
// COMMON: call ptr @llvm.preserve.union.access.index.p0.p0(ptr %{{[0-9a-z]+}}, i32 1), !dbg [[UNIT2_UNION_LOC:![0-9]+]], !llvm.preserve.access.index ![[UNION_U1:[0-9]+]]
//
// OGCG-DAG: [[UNIT1_STRUCT_LOC]] = !DILocation(line: 14, column: 18, scope: !{{[0-9]+}})
// OGCG-DAG: [[UNIT1_ARRAY_LOC]] = !DILocation(line: 14, column: 13, scope: !{{[0-9]+}})
// OGCG-DAG: [[UNIT2_UNION_LOC]] = !DILocation(line: 26, column: 18, scope: !{{[0-9]+}})
// CIR-DAG: [[UNIT1_STRUCT_LOC]] = !DILocation(line: 14, column: 18, scope: !{{[0-9]+}})
// CIR-DAG: [[UNIT1_ARRAY_LOC]] = !DILocation(line: 14, column: 13, scope: !{{[0-9]+}})
// CIR-DAG: [[UNIT2_UNION_LOC]] = !DILocation(line: 26, column: 18, scope: !{{[0-9]+}})
// COMMON-DAG: ![[ARRAY]] = !DICompositeType(tag: DW_TAG_array_type
// COMMON-DAG: ![[STRUCT_S1]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "s1"
// COMMON-DAG: ![[UNION_U1]] = distinct !DICompositeType(tag: DW_TAG_union_type, name: "u1"
