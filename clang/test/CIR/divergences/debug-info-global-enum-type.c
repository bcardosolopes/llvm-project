// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CHECK
//
//
//
// Former divergence: CIR used to degrade enum globals to a plain integer type
// instead of preserving the enumeration debug type and enumerators.

enum E { A = 1, B = 2 };
enum E ge = B;

// CHECK: ![[GV:[0-9]+]] = distinct !DIGlobalVariable(name: "ge", scope: ![[CU:[0-9]+]], file: ![[FILE:[0-9]+]], line: {{[0-9]+}}, type: ![[ENUM:[0-9]+]], isLocal: false, isDefinition: true)
// CHECK: ![[ENUM]] = !DICompositeType(tag: DW_TAG_enumeration_type, name: "E", file: ![[FILE]], line: {{[0-9]+}}, baseType: ![[BASE:[0-9]+]], size: 32, elements: ![[ELEMS:[0-9]+]])
// CHECK: ![[BASE]] = !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
// CHECK: ![[ELEMS]] = !{![[A:[0-9]+]], ![[B:[0-9]+]]}
// CHECK: ![[A]] = !DIEnumerator(name: "A", value: 1)
// CHECK: ![[B]] = !DIEnumerator(name: "B", value: 2)
