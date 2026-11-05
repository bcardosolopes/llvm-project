// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple amdgcn-unknown-unknown -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that structs with flexible array members are passed indirectly (byval)
// on AMDGPU, matching OGCG's DefaultABIInfo::classifyArgumentType behavior.
// They must not be passed as direct/flattened to multiple parameters.

typedef struct {
  int a;
  int b[];
} flexible_array;

// CIR: define {{.*}}void @func_flexible_array_arg(ptr {{.*}}byval(%struct.flexible_array) align 4
// OGCG: define {{.*}}void @func_flexible_array_arg(ptr {{.*}}byval(%struct.flexible_array) align 4
void func_flexible_array_arg(flexible_array arg) { }

// Return with flexible array should use sret (indirect return).
// CIR: define {{.*}}void @func_flexible_array_ret(ptr {{.*}}sret(%struct.flexible_array) align 4
// OGCG: define {{.*}}void @func_flexible_array_ret(ptr {{.*}}sret(%struct.flexible_array) align 4
flexible_array func_flexible_array_ret() {
  flexible_array result = {0};
  return result;
}
