// RUN: %clang_cc1 -emit-llvm -debug-info-kind=limited -I%p %s -o - | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -fclangir -emit-llvm -debug-info-kind=limited -I%p %s -o - | FileCheck %s %}
// Test that the typedef points to the header file while the global definition
// stays attributed to the source file.
#line 1 "a.c"
#line 2 "b.h"
typedef int MyType;
#line 2 "a.c"

MyType a;

// CHECK: ![[VAR:[0-9]+]] = distinct !DIGlobalVariable(name: "a", scope: !{{[0-9]+}}, file: ![[SOURCE:[0-9]+]], line: 3, type: ![[TYPE:[0-9]+]], isLocal: false, isDefinition: true)
// CHECK: ![[SOURCE]] = !DIFile(filename: "a.c",
// CHECK: ![[TYPE]] = !DIDerivedType(tag: DW_TAG_typedef, name: "MyType", file: ![[HEADER:[0-9]+]], line: 2,
// CHECK: ![[HEADER]] = !DIFile(filename: "b.h",
