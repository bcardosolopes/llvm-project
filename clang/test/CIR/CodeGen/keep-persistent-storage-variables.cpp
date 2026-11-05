// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fkeep-persistent-storage-variables -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fkeep-persistent-storage-variables -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Verify static locals are preserved in llvm.compiler.used when
// -fkeep-persistent-storage-variables is enabled.

int f() {
  static int s = 1;
  return s++;
}

// CIR: @_ZZ1fvE1s = internal global i32 1, align 4
// CIR: @llvm.compiler.used = appending global [1 x ptr] [ptr @_ZZ1fvE1s], section "llvm.metadata"

// OGCG: @_ZZ1fvE1s = internal global i32 1, align 4
// OGCG: @llvm.compiler.used = appending global [1 x ptr] [ptr @_ZZ1fvE1s], section "llvm.metadata"
