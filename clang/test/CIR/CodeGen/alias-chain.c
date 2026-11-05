// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test that chained weak aliases are resolved to point directly to the
// ultimate definition, matching OGCG behavior. LLVM requires that aliases
// do not point to interposable (weak) aliases.

#pragma weak alias1 = func
#pragma weak alias3 = alias2
#pragma weak alias4 = alias2
#pragma weak alias2 = func

void alias2(void);

void func(void) {}

// CIR-DAG: cir.alias weak weak @alias3 = @alias2
// CIR-DAG: cir.alias weak weak @alias4 = @alias2
// CIR-DAG: cir.alias weak weak @alias1 = @func
// CIR-DAG: cir.alias weak weak @alias2 = @func

// All aliases must point directly to @func in the final LLVM IR,
// not to each other (LLVM forbids pointing to interposable aliases).
// LLVM-DAG: @alias1 = weak alias void (), ptr @func
// LLVM-DAG: @alias2 = weak alias void (), ptr @func
// LLVM-DAG: @alias3 = weak alias void (), ptr @func
// LLVM-DAG: @alias4 = weak alias void (), ptr @func

// OGCG-DAG: @alias1 = weak alias void (), ptr @func
// OGCG-DAG: @alias2 = weak alias void (), ptr @func
// OGCG-DAG: @alias3 = weak alias void (), ptr @func
// OGCG-DAG: @alias4 = weak alias void (), ptr @func
