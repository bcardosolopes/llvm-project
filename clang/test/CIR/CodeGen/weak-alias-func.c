// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Test that a weak alias function declaration followed by a use of the
// function compiles correctly. Previously this crashed because
// getOrCreateCIRFunction asserted that the existing symbol was a FuncOp
// when it was actually an AliasOp.

int x(void) { return 1; }
int f(void) __attribute__((weak, alias("x")));
int f(void);
int h(void) { return f(); }

// CIR: cir.func {{.*}} @x()
// CIR: cir.func {{.*}} @h()
// CIR:   cir.call @f()
// CIR: cir.func weak {{.*}} @f() {{.*}} alias(@x)

// LLVM: @f = weak alias i32 (), ptr @x
// LLVM: define{{.*}} i32 @h()
// LLVM:   {{.*}} = call i32 @f()
