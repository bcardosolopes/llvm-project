// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that globals initialized with label address differences (block_addr_diff)
// are correctly handled by the CIR verifier for various integer types.

// Foldable case: the difference fits in the target type (long / i64).
// Pretty form now: the self type is printed once after the closing `>` by
// MLIR, rather than the generic `#cir<...>` wrapper the old format forced.
// CHECK-DAG: cir.global "private"  internal {{.*}} @_ZZ21FoldableAddrLabelDiffvE1x = #cir.block_addr_diff<<@_Z21FoldableAddrLabelDiffv, "a">, <@_Z21FoldableAddrLabelDiffv, "b">> : !s64i
void FoldableAddrLabelDiff() { static long x = (long)&&a-(long)&&b; a:b:return; }

// Unfoldable: target type is too narrow (short / i16), so we get zero-init.
// CHECK-DAG: cir.global "private"  internal {{.*}} @_ZZ24UnfoldableAddrLabelDiff2vE1x = #cir.int<0> : !s16i
void UnfoldableAddrLabelDiff2() { static short x = (long)&&a-(long)&&b; a:b:return; }
