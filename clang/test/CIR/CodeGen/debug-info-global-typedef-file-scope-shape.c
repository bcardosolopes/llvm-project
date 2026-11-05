// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s

// Regression coverage:
// - preserve typedef source file as full-path DIFile(filename=..., directory="")
// - avoid injecting scope/file/line on qualifier/vector nodes that OGCG keeps
//   unscoped in this global-typedef path

typedef const int cint;
typedef int V4 __attribute__((vector_size(16)));

cint gc = 7;
V4 gv;

// CHECK-DAG: !DIGlobalVariable(name: "gc", scope: ![[CU:[0-9]+]], file: ![[GFILE:[0-9]+]], line: 11, type: ![[CINT:[0-9]+]]
// CHECK-DAG: !DIGlobalVariable(name: "gv", scope: ![[CU]], file: ![[GFILE]], line: 12, type: ![[V4:[0-9]+]]
// This bare -cc1 invocation omits -main-file-name, so the compile unit's file is
// a synthetic "<stdin>" basename (matching classic CodeGen), while the
// globals/typedefs reference the real source DIFile.
// CHECK-DAG: ![[CU]] = distinct !DICompileUnit(language: DW_LANG_C11, file: ![[CUFILE:[0-9]+]]
// CHECK-DAG: ![[CUFILE]] = !DIFile(filename: "{{.*}}clang/test/CIR/CodeGen/<stdin>", directory: "")
// CHECK-DAG: ![[GFILE]] = !DIFile(filename: "{{.*}}clang/test/CIR/CodeGen/debug-info-global-typedef-file-scope-shape.c", directory: "")
//
// Typedef wrappers should use a full-path DIFile and no explicit `scope:`.
// CHECK-DAG: ![[CINT]] = !DIDerivedType(tag: DW_TAG_typedef, name: "cint", file: ![[GFILE]], line: 8, baseType: ![[CONST:[0-9]+]])
// CHECK-DAG: ![[V4]] = !DIDerivedType(tag: DW_TAG_typedef, name: "V4", file: ![[GFILE]], line: 9, baseType: ![[VEC:[0-9]+]])
//
// Qualifier/vector nodes should remain canonical and unscoped.
// CHECK-DAG: ![[CONST]] = !DIDerivedType(tag: DW_TAG_const_type, baseType: ![[INT:[0-9]+]])
// CHECK-DAG: ![[VEC]] = !DICompositeType(tag: DW_TAG_array_type, baseType: ![[INT]], size: 128, flags: DIFlagVector, elements: ![[ELEMS:[0-9]+]])
// CHECK-DAG: ![[ELEMS]] = !{![[SUB:[0-9]+]]}
// CHECK-DAG: ![[SUB]] = !DISubrange(count: 4)
