// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG
// Union return ABI — CIR missing coerce-dive, returns i64 instead of ptr

union X { char X; void *B; int a, b, c, d; };

union X foo(void) {
  union X Global;
  Global.B = (void*)123;
  return Global;
}

// LLVM: define dso_local ptr @foo()
// OGCG: define dso_local ptr @foo()
