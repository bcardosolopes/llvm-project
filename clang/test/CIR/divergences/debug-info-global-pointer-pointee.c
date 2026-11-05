// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CHECK
//
//
//
// Former divergence: CIR used to degrade the pointer pointee debug type to
// void instead of preserving `int *`. The remaining full-file diff noise here
// is non-semantic compile-unit globals ordering.

int x;
int *gp = &x;

// CHECK: ![[X:[0-9]+]] = distinct !DIGlobalVariable(name: "x", scope: ![[CU:[0-9]+]], file: ![[FILE:[0-9]+]], line: {{[0-9]+}}, type: ![[INT:[0-9]+]], isLocal: false, isDefinition: true)
// CHECK: ![[GP:[0-9]+]] = distinct !DIGlobalVariable(name: "gp", scope: ![[CU]], file: ![[FILE]], line: {{[0-9]+}}, type: ![[PTR:[0-9]+]], isLocal: false, isDefinition: true)
// CHECK: ![[PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[INT]], size: 64)
// CHECK: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
