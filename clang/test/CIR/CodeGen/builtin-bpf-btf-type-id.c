// RUN: %clang_cc1 -triple bpfel -fclangir -emit-llvm -disable-llvm-passes -debug-info-kind=limited %s -o - | FileCheck %s --check-prefixes=CHECK,META
// RUN: %clang_cc1 -triple bpfel -emit-llvm -disable-llvm-passes -debug-info-kind=limited %s -o - | FileCheck %s --check-prefixes=CHECK,META

unsigned test1(int a) { return __builtin_btf_type_id(a, 0); }
unsigned test2(int a) { return __builtin_btf_type_id(&a, 0); }

struct t1 {
  int a;
};
typedef struct t1 __t1;
unsigned test3(void) {
  return __builtin_btf_type_id(*(struct t1 *)0, 1) +
         __builtin_btf_type_id(*(__t1 *)0, 1);
}

struct s1 {
  int a;
};
unsigned long test4(void) {
  return __builtin_preserve_type_info(*(struct s1 *)0, 0);
}

// CHECK: define dso_local i32 @test1
// CHECK: call i64 @llvm.bpf.btf.type.id(i32 {{[0-9]+}}, i64 0), !dbg !{{[0-9]+}}, !llvm.preserve.access.index ![[TEST1_TYPE:[0-9]+]]
// CHECK: define dso_local i32 @test2
// CHECK: call i64 @llvm.bpf.btf.type.id(i32 {{[0-9]+}}, i64 0), !dbg !{{[0-9]+}}, !llvm.preserve.access.index ![[TEST2_TYPE:[0-9]+]]
// CHECK: define dso_local i32 @test3
// CHECK: call i64 @llvm.bpf.btf.type.id(i32 {{[0-9]+}}, i64 1), !dbg !{{[0-9]+}}, !llvm.preserve.access.index ![[TEST3_STRUCT:[0-9]+]]
// CHECK: call i64 @llvm.bpf.btf.type.id(i32 {{[0-9]+}}, i64 1), !dbg !{{[0-9]+}}, !llvm.preserve.access.index ![[TEST3_TYPEDEF:[0-9]+]]
// CHECK: define dso_local i64 @test4
// CHECK: call i32 @llvm.bpf.preserve.type.info(i32 {{[0-9]+}}, i64 0), !dbg !{{[0-9]+}}, !llvm.preserve.access.index ![[TEST4_TYPE:[0-9]+]]
// CHECK: zext i32 %{{[0-9a-z]+}} to i64, !dbg !{{[0-9]+}}

// META-DAG: ![[TEST1_TYPE]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed
// META-DAG: ![[TEST2_TYPE]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[TEST1_TYPE]], size: 64
// META-DAG: ![[TEST3_TYPEDEF]] = !DIDerivedType(tag: DW_TAG_typedef, name: "__t1", file: !{{[0-9]+}}, line: 10, baseType: ![[TEST3_STRUCT]]
// META-DAG: ![[TEST3_STRUCT]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "t1"
// META-DAG: ![[TEST4_TYPE]] = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "s1"
