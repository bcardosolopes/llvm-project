// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s

// Regression for deferred block-address global initializer fixup.
// CIR should lower the aggregate initializer containing &&L into an LLVM
// constant with blockaddress instead of failing during CIR-to-LLVM lowering.
// CHECK: @.compoundLiteral.0 = private unnamed_addr constant [3 x ptr] [ptr blockaddress(@main, %[[L:[0-9]+]]), ptr null, ptr null], align 8

void receivePtrs(void **);

int main(void) {
L:
  receivePtrs((void *[]){ &&L, 0, 0 });
  return 0;
}

// CHECK-LABEL: define dso_local i32 @main()
// CHECK: [[L]]:
