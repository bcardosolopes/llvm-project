// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// Test that i128 atomicrmw operations are lowered correctly.

__int128 x;

__int128 test_xor(__int128 val) {
  return __sync_fetch_and_xor(&x, val);
}
// LLVM: atomicrmw xor ptr @x, i128 {{.*}} seq_cst
// OGCG: atomicrmw xor ptr @x, i128 {{.*}} seq_cst

__int128 test_and(__int128 val) {
  return __sync_fetch_and_and(&x, val);
}
// LLVM: atomicrmw and ptr @x, i128 {{.*}} seq_cst
// OGCG: atomicrmw and ptr @x, i128 {{.*}} seq_cst

__int128 test_or(__int128 val) {
  return __sync_fetch_and_or(&x, val);
}
// LLVM: atomicrmw or ptr @x, i128 {{.*}} seq_cst
// OGCG: atomicrmw or ptr @x, i128 {{.*}} seq_cst

__int128 test_nand_and_fetch(__int128 val) {
  return __sync_nand_and_fetch(&x, val);
}
// The nand_and_fetch returns the NEW value: ~(old & val).
// atomicrmw nand returns the OLD value, so the lowering recomputes the result.
// The XOR mask must be all-ones for the full i128 width, not just 64 bits.
// LLVM: atomicrmw nand ptr @x, i128 {{.*}} seq_cst
// LLVM: xor i128 {{.*}}, -1
// OGCG: atomicrmw nand ptr @x, i128 {{.*}} seq_cst
// OGCG: xor i128 {{.*}}, -1
