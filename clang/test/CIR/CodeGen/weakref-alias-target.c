// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Regression test for weakref aliasee lookup when aliasee name resolves to a
// variable alias symbol. Before this crashed in
// CIRGenModule::getWeakRefReference with llvm_unreachable.

int z;
extern int y __attribute__((alias("z")));
static int x __attribute__((weakref("y")));
int *p = &x;

// CIR: cir.alias external @y = @z : !s32i
// CIR: cir.global  external  @z = #cir.int<0> : !s32i
// CIR: cir.global  external  @p = #cir.global_view<@z> : !cir.ptr<!s32i>

// LLVM-DAG: @z = global i32 0, align 4
// LLVM-DAG: @y = alias i32, ptr @z
// LLVM-DAG: @p = global ptr @z, align 8

// OGCG-DAG: @z = global i32 0, align 4
// OGCG-DAG: @y = alias i32, ptr @z
// OGCG-DAG: @p = global ptr @y, align 8
