// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// __builtin_debugtrap must be handled by the generic builtin emitter on every
// target. It was only handled for __debugbreak, so __builtin_debugtrap fell
// through to the target-specific builtin dispatch; on AArch64 that path reads
// CallExpr::getArg(0) and aborted ("Arg access out of range") because
// __builtin_debugtrap takes no arguments.

void f(void) { __builtin_debugtrap(); }

// LLVM-LABEL: @f
// LLVM: call void @llvm.debugtrap()

// OGCG-LABEL: @f
// OGCG: call void @llvm.debugtrap()
