// RUN: %clang_cc1 -triple aarch64-none-linux-android26 -fclangir -debug-info-kind=limited -emit-llvm -o - %s | FileCheck %s
// REQUIRES: cir-support

struct S {
  int x;
};

union U {
  int x;
};

void struct_param(struct S params[restrict 1]) {}
void union_param(union U params[restrict 1]) {}

// CHECK: define dso_local void @struct_param(ptr noalias noundef
// CHECK: define dso_local void @union_param(ptr noalias noundef
// CHECK: ![[STRUCT_SUB:[0-9]+]] = distinct !DISubprogram(name: "struct_param"
// CHECK: ![[STRUCT_SUBTY:[0-9]+]] = !DISubroutineType(types: ![[STRUCT_TYPES:[0-9]+]])
// CHECK: ![[STRUCT_TYPES]] = !{null, ![[STRUCT_RESTRICT:[0-9]+]]}
// CHECK: ![[STRUCT_RESTRICT]] = !DIDerivedType(tag: DW_TAG_restrict_type, baseType: ![[STRUCT_PTR:[0-9]+]])
// CHECK: ![[STRUCT_PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[STRUCT_REC:[0-9]+]], size: 64)
// CHECK: ![[STRUCT_REC]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "S"
// CHECK: ![[STRUCT_PARAM:[0-9]+]] = !DILocalVariable(name: "params", arg: 1, scope: ![[STRUCT_SUB]], {{.*}}type: ![[STRUCT_RESTRICT]])
// CHECK: ![[UNION_SUB:[0-9]+]] = distinct !DISubprogram(name: "union_param"
// CHECK: ![[UNION_SUBTY:[0-9]+]] = !DISubroutineType(types: ![[UNION_TYPES:[0-9]+]])
// CHECK: ![[UNION_TYPES]] = !{null, ![[UNION_RESTRICT:[0-9]+]]}
// CHECK: ![[UNION_RESTRICT]] = !DIDerivedType(tag: DW_TAG_restrict_type, baseType: ![[UNION_PTR:[0-9]+]])
// CHECK: ![[UNION_PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[UNION_REC:[0-9]+]], size: 64)
// CHECK: ![[UNION_REC]] = distinct !DICompositeType(tag: DW_TAG_union_type, name: "U"
// CHECK: ![[UNION_PARAM:[0-9]+]] = !DILocalVariable(name: "params", arg: 1, scope: ![[UNION_SUB]], {{.*}}type: ![[UNION_RESTRICT]])
