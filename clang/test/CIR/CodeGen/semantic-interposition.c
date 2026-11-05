// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -fsemantic-interposition -emit-llvm -o %t.ll %s
// RUN: FileCheck %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsemantic-interposition -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck %s -input-file=%t.ogcg.ll

// -fsemantic-interposition must emit SemanticInterposition module flag.
void f(void) {}

// CHECK: !{i32 1, !"SemanticInterposition", i32 1}
