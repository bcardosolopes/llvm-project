// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Global initialization functions (__cxx_global_var_init and
// _GLOBAL__sub_I_*) are emitted with section ".text.startup" on ELF targets.
// CIR sets this via getStaticInitSectionSpecifier() in LoweringPrepare.

int foo();
int x = foo();

// CHECK: define internal void @__cxx_global_var_init() {{.*}}section ".text.startup"
// CHECK: define internal void @_GLOBAL__sub_I_{{.*}}() {{.*}}section ".text.startup"
