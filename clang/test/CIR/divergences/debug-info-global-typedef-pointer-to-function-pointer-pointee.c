// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CHECK
//
//
//
// Former divergence: CIR used to lower the pointer-to-function-pointer pointee
// chain to void* instead of preserving the nested typedef'd function pointer.

typedef int (*FN)(int);
typedef FN *PFN;
PFN gpf;

// CHECK: ![[GV:[0-9]+]] = distinct !DIGlobalVariable(name: "gpf", scope: ![[CU:[0-9]+]], file: ![[FILE:[0-9]+]], line: {{[0-9]+}}, type: ![[PFN:[0-9]+]], isLocal: false, isDefinition: true)
// CHECK: ![[PFN]] = !DIDerivedType(tag: DW_TAG_typedef, name: "PFN", file: ![[FILE]], line: {{[0-9]+}}, baseType: ![[OUTER_PTR:[0-9]+]])
// CHECK: ![[OUTER_PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[FN:[0-9]+]], size: 64)
// CHECK: ![[FN]] = !DIDerivedType(tag: DW_TAG_typedef, name: "FN", file: ![[FILE]], line: {{[0-9]+}}, baseType: ![[INNER_PTR:[0-9]+]])
// CHECK: ![[INNER_PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[SUB:[0-9]+]], size: 64)
// CHECK: ![[SUB]] = !DISubroutineType(types: ![[TYPES:[0-9]+]])
// CHECK: ![[TYPES]] = !{![[INT:[0-9]+]], ![[INT]]}
// CHECK: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
