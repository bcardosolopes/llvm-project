// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Regression test: taking the address of a variable alias used in a
// global initializer must keep the alias symbol (instead of erasing it) and
// emit both the aliasee and the referring global.

int z;
extern int y __attribute__((alias("z")));
int *p = &y;

// CIR-DAG: cir.global  external  @z = #cir.int<0> : !s32i
// CIR-DAG: cir.alias external @y = @z : !s32i
// CIR-DAG: cir.global  external  @p = #cir.global_view<@y> : !cir.ptr<!s32i>

// LLVM-DAG: @z = global i32 0, align 4
// LLVM-DAG: @y = alias i32, ptr @z
// LLVM-DAG: @p = global ptr @y, align 8

// OGCG-DAG: @z = global i32 0, align 4
// OGCG-DAG: @y = alias i32, ptr @z
// OGCG-DAG: @p = global ptr @y, align 8
