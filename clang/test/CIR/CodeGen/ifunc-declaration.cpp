// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Calling foo() before its ifunc declaration causes CIR to create a placeholder
// declaration FuncOp first. When the ifunc declaration is later processed, it
// must clear dso_local on that reused symbol to match OGCG.

extern "C" void *resolve_foo() { return 0; }
void *foo();
void *use() { return foo(); }
__attribute__((ifunc("resolve_foo"))) void *foo();

// CIR: cir.func private @_Z3foov() -> (!cir.ptr<!void> {cir.noundef}) alias(@resolve_foo){{.*}}attributes {cir.ifunc}
// CIR-NOT: cir.func private dso_local @_Z3foov

// LLVM: @_Z3foov = ifunc ptr (), ptr @resolve_foo
// LLVM-NOT: @_Z3foov = dso_local ifunc
// LLVM: define dso_local noundef ptr @_Z3usev()

// OGCG: @_Z3foov = ifunc ptr (), ptr @resolve_foo
