// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnux32 -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.i386.ll
// RUN: FileCheck --input-file=%t.i386.ll %s --check-prefix=CIR-X32
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnux32 -emit-llvm \
// RUN:   -debug-info-kind=limited %s -o %t.ogcg.i386.ll
// RUN: FileCheck --input-file=%t.ogcg.i386.ll %s --check-prefix=OGCG-X32

// Test that CIR emits function subroutine type info in debug metadata.
// Functions should have DISubroutineType with parameter/return types.
// Also verify pointer debug type size follows DataLayout on 32-bit targets.

int add(int a, int b) { return a + b; }

// CIR-DAG: !DISubprogram(name: "add", linkageName: "_Z3addii"
// CIR-DAG: !DISubroutineType({{.*}}types: ![[ADD_TYPES:[0-9]+]])
// CIR-DAG: ![[ADD_TYPES]] = !{![[INT:[0-9]+]], ![[INT]], ![[INT]]}
// CIR-DAG: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)

// OGCG-DAG: !DISubprogram(name: "add", linkageName: "_Z3addii"
// OGCG-DAG: !DISubroutineType(types: ![[ADD_TYPES:[0-9]+]])
// OGCG-DAG: ![[ADD_TYPES]] = !{![[INT:[0-9]+]], ![[INT]], ![[INT]]}
// OGCG-DAG: ![[INT]] = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)

double scale(double x) { return x * 2.0; }

// CIR-DAG: !DIBasicType(name: "double", size: 64, encoding: DW_ATE_float)
// OGCG-DAG: !DIBasicType(name: "double", size: 64, encoding: DW_ATE_float)

void noop() {}

// CIR-DAG: !DISubprogram(name: "noop", linkageName: "_Z4noopv", scope: ![[#]], file: ![[#]], line: 35, type: ![[#]], scopeLine: 35, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: ![[#]])
// OGCG-DAG: !DISubprogram(name: "noop", linkageName: "_Z4noopv", scope: ![[#]], file: ![[#]], line: 35, type: ![[#]], scopeLine: 35, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: ![[#]])

int *ret_ptr(int *p) { return p; }

// CIR-DAG: !DISubprogram(name: "ret_ptr", linkageName: "_Z7ret_ptrPi"
// OGCG-DAG: !DISubprogram(name: "ret_ptr", linkageName: "_Z7ret_ptrPi"

// CIR-X32-DAG: !DIDerivedType(tag: DW_TAG_pointer_type,{{.*}}size: 32)
// OGCG-X32-DAG: !DIDerivedType(tag: DW_TAG_pointer_type,{{.*}}size: 32)
