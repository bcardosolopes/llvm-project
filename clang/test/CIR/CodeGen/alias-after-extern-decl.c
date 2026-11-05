// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Test: extern declaration followed by alias definition.
// The extern decl creates a FuncOp, then the alias attribute must convert
// it into an alias rather than creating a duplicate symbol.

extern int test6(void);
void test7(void) { test6(); }
int test6(void) __attribute__((alias("test7")));

// CIR: cir.func {{.*}} @test7()
// CIR: cir.func private @test6() -> !s32i alias(@test7)

// LLVM: @test6 = alias i32 (), ptr @test7
// LLVM: define dso_local void @test7()

// OGCG: @test6 = alias i32 (), ptr @test7
// OGCG: define dso_local void @test7()
