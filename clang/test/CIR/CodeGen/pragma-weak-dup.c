// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// Duplicate #pragma weak causes symbol redefinition

// Test duplicate #pragma weak after definition
void __stutter2(void) {}
#pragma weak stutter2 = __stutter2
#pragma weak stutter2 = __stutter2

// Test duplicate #pragma weak before definition
#pragma weak stutter = __stutter
#pragma weak stutter = __stutter
void __stutter(void) {}

// Only one alias per name should exist — no duplicates.
// CIR-COUNT-1: cir.alias {{.*}} @stutter2 = @__stutter2
// CIR-COUNT-1: cir.alias {{.*}} @stutter = @__stutter

// LLVM-DAG: @stutter2 = weak alias void (), ptr @__stutter2
// LLVM-DAG: @stutter = weak alias void (), ptr @__stutter
// LLVM: define{{.*}} void @__stutter2()
// LLVM: define{{.*}} void @__stutter()
