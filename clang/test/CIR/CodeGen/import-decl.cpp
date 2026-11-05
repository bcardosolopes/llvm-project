// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu -std=c++20 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -emit-llvm -triple x86_64-unknown-linux-gnu -std=c++20 %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s

// Test that Import declarations from #pragma clang module import are handled.

#pragma clang module build M
module M {}
#pragma clang module endbuild
#pragma clang module import M

// An empty module import should compile without errors.
// Just verify we produce a valid module.
// CHECK: target triple
