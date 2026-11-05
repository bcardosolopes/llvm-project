// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mdefault-visibility-export-mapping=all -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -mdefault-visibility-export-mapping=all -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that -mdefault-visibility-export-mapping=all applies
// dllexport to plain functions (not just explicit __declspec(dllexport)).
// This exercises the shouldMapVisibilityToDLLExport path added.

// CIR: define dso_local dllexport void @func()
// OGCG: define dso_local dllexport void @func()
void func() {}

// CIR: define dso_local dllexport void @explicitfunc()
// OGCG: define dso_local dllexport void @explicitfunc()
void __attribute__((visibility("default"))) explicitfunc() {}
