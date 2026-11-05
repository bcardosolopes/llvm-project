// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=OGCG

// A function with a non-C calling convention records DW_AT_calling_convention on
// its DISubroutineType, matching classic CodeGen (getDwarfCC). CIR previously
// dropped the calling convention, emitting a plain (DW_CC_normal) subroutine
// type. The C convention stays omitted.

__attribute__((preserve_most)) int pm(int x) { return x + 1; }
__attribute__((preserve_all)) int pa(int x) { return x + 2; }
int normal(int x) { return x + 3; }

int use(void) { return pm(1) + pa(2) + normal(3); }

// CHECK-DAG: !DISubroutineType(cc: DW_CC_LLVM_PreserveMost, types: ![[#]])
// CHECK-DAG: !DISubroutineType(cc: DW_CC_LLVM_PreserveAll, types: ![[#]])

// OGCG-DAG: !DISubroutineType(cc: DW_CC_LLVM_PreserveMost, types: ![[#]])
// OGCG-DAG: !DISubroutineType(cc: DW_CC_LLVM_PreserveAll, types: ![[#]])
