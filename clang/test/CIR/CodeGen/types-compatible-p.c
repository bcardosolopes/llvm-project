// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// __builtin_types_compatible_p previously produced CIR verification
//        error (cir.store type mismatch: bool vs int). Fixed by emitting
//        integer constant matching the expression's type.
int types_compat_test(void) {
  return __builtin_types_compatible_p(int, signed int);
}

// LLVM: define{{.*}} i32 @types_compat_test()
