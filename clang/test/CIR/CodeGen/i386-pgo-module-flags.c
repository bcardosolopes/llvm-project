// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fprofile-instrument=clang \
// RUN:   -fcoverage-mapping -fclangir -emit-llvm %s -o - | FileCheck %s

// On i386 the target-specific NumRegisterParameters module flag is emitted by
// CIR lowering, and profile/coverage instrumentation augments the module with
// an additional copy. Module flag identifiers must be unique, so the duplicate
// previously failed the verifier with "Broken module found". The lowering now
// drops duplicate module flag keys, so the flag appears exactly once.

int f(int x) { return x + 1; }

// There must be exactly one NumRegisterParameters module flag.
// CHECK:      !"NumRegisterParameters"
// CHECK-NOT:  !"NumRegisterParameters"
