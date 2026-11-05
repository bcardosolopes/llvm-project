// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CHECK
//
//
//
// Former divergence: CIR used to lose the typedef -> array -> typedef'd
// function-pointer chain and collapse the array element type.

typedef int (*FNTY)(int);
typedef FNTY FNARR[2];
FNARR gfa;

// CHECK: ![[GV:[0-9]+]] = distinct !DIGlobalVariable(name: "gfa", scope: ![[CU:[0-9]+]], file: ![[FILE:[0-9]+]], line: {{[0-9]+}}, type: ![[FNARR:[0-9]+]], isLocal: false, isDefinition: true)
// CHECK: ![[FNARR]] = !DIDerivedType(tag: DW_TAG_typedef, name: "FNARR", file: ![[FILE]], line: {{[0-9]+}}, baseType: ![[ARR:[0-9]+]])
// CHECK: ![[ARR]] = !DICompositeType(tag: DW_TAG_array_type, baseType: ![[FNTY:[0-9]+]], size: 128, elements: ![[RANGES:[0-9]+]])
// CHECK: ![[FNTY]] = !DIDerivedType(tag: DW_TAG_typedef, name: "FNTY", file: ![[FILE]], line: {{[0-9]+}}, baseType: ![[PTR:[0-9]+]])
// CHECK: ![[PTR]] = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[SUB:[0-9]+]], size: 64)
// CHECK: ![[SUB]] = !DISubroutineType(types: ![[TYPES:[0-9]+]])
// CHECK: ![[TYPES]] = !{![[INT:[0-9]+]], ![[INT]]}
// CHECK: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
