// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s < %t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=LLVM %s < %t.cir.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG %s < %t.ogcg.ll

// Verify that __attribute__((noconvergent)) removes the convergent attribute
// from functions that are convergent by default (e.g., in OpenCL/CUDA modes).

// A function explicitly marked convergent.
__attribute__((convergent)) void convergent_func(void);

// A function marked both convergent and noconvergent — noconvergent wins.
__attribute__((convergent, noconvergent)) void noconvergent_func(void);

void caller(void) {
  convergent_func();
  noconvergent_func();
}

// CIR: cir.func {{.*}}@caller
// CIR: cir.func {{.*}}@noconvergent_func{{.*}}extra(#fn_attr
// CIR: cir.func {{.*}}@convergent_func{{.*}}extra(#fn_attr

// The noconvergent function should NOT have convergent.
// LLVM: declare void @noconvergent_func() #[[NOCONVATTR:[0-9]+]]
// The convergent function should have convergent in LLVM IR.
// LLVM: declare void @convergent_func() #[[CONVATTR:[0-9]+]]
// LLVM: attributes #[[CONVATTR]] = {{{.*}}convergent{{.*}}}
// LLVM-NOT: attributes #[[NOCONVATTR]] = {{{.*}}convergent{{.*}}}

// OGCG: declare void @convergent_func() #[[CONVATTR:[0-9]+]]
// OGCG: declare void @noconvergent_func() #[[NOCONVATTR:[0-9]+]]
// OGCG: attributes #[[CONVATTR]] = {{{.*}}convergent{{.*}}}
// OGCG-NOT: attributes #[[NOCONVATTR]] = {{{.*}}convergent{{.*}}}
