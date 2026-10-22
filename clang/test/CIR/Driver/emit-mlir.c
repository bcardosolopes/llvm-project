// -emit-mlir= forwards its kind to cc1.
//
// RUN: %clang -### -fclangir -emit-mlir=cir-flat -c %s 2>&1 | FileCheck %s --check-prefix=EQ
// EQ: "-cc1"
// EQ-SAME: "-emit-mlir=cir-flat"
// EQ-NOT: "-emit-cir"

// RUN: not %clang_cc1 -fclangir -emit-mlir=bogus %s -o - 2>&1 | FileCheck %s --check-prefix=BAD
// BAD: invalid value 'bogus' in '-emit-mlir=bogus'

int f() { return 0; }
