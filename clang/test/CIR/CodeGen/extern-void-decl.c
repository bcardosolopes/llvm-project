// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// GCC extension: extern void declaration (PR4556)
extern void foo;
void *bar = &foo;

// CIR: cir.global "private"  external  @foo : !void
// CIR: cir.global  external  @bar = #cir.global_view<@foo> : !cir.ptr<!void>

// LLVM: @foo = external global i8, align 1
// LLVM: @bar = global ptr @foo, align 8

// OGCG: @foo = external global i8, align 1
// OGCG: @bar = global ptr @foo, align 8
