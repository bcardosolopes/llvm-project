// RUN: %clang_cc1 %s -triple x86_64-linux-gnu -fclangir -emit-llvm -o %t.cir.ll
// RUN: %clang_cc1 %s -triple x86_64-linux-gnu -emit-llvm -o %t.ogcg.ll
// RUN: %clang_cc1 %s -triple x86_64-linux-gnu -fclangir -emit-cir -o %t.cir.mlir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s
// RUN: FileCheck --check-prefix=CIR-MLIR --input-file=%t.cir.mlir %s

// Verify that internal-linkage declarations with __attribute__((used)) inside
// extern "C" blocks get unmangled aliases and llvm.compiler.used entries.

extern "C" {
  // Unused statics should NOT get aliases.
  static int unused_var;
  static int unused_fn() { return 0; }

  // Used statics with unique names should get aliases.
  __attribute__((used)) static int internal_var;
  __attribute__((used)) static int internal_fn() { return 0; }

  // Duplicate names across different scopes should suppress aliases.
  __attribute__((used)) static int duplicate_internal_var;
  __attribute__((used)) static int duplicate_internal_fn() { return 0; }

  namespace N {
    __attribute__((used)) static int duplicate_internal_var;
    __attribute__((used)) static int duplicate_internal_fn() { return 0; }
  }
}

// CIR: @llvm.compiler.used = appending global {{.*}} @internal_var, {{.*}} @internal_fn
// OGCG: @llvm.compiler.used = appending global {{.*}} @internal_var, {{.*}} @internal_fn

// CIR-NOT: @unused
// CIR-NOT: @duplicate_internal
// CIR: @internal_var = internal alias i32, ptr @_ZL12internal_var
// CIR-NOT: @unused
// CIR-NOT: @duplicate_internal
// CIR: @internal_fn = internal alias i32 (), ptr @_ZL11internal_fnv
// CIR-NOT: @unused
// CIR-NOT: @duplicate_internal

// CIR-MLIR: cir.alias internal @internal_var = @_ZL12internal_var
// CIR-MLIR: cir.alias internal @internal_fn = @_ZL11internal_fnv

// OGCG-NOT: @unused
// OGCG-NOT: @duplicate_internal
// OGCG: @internal_var = internal alias i32, ptr @_ZL12internal_var
// OGCG-NOT: @unused
// OGCG-NOT: @duplicate_internal
// OGCG: @internal_fn = internal alias i32 (), ptr @_ZL11internal_fnv
// OGCG-NOT: @unused
// OGCG-NOT: @duplicate_internal
