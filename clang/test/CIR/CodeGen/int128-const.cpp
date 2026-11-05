// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Test that __int128 integer constants wider than 64 bits are handled
// correctly in CIR codegen, printing, and LLVM lowering. This verifies
// the fix for APSInt::getExtValue() assertion failures on wide integers.

// CHECK: cir.func {{.*}} @_Z7get_bigv
// LLVM: define {{.*}} @_Z7get_bigv
static constexpr __int128 big_val =
    ((__int128)0x7FFFFFFFFFFFFFFFLL << 64) | 0xFFFFFFFFFFFFFFFFULL;
__int128 get_big() {
  // CHECK: cir.const #cir.int<170141183460469231731687303715884105727> : !s128i
  // LLVM: ret i128 170141183460469231731687303715884105727
  return big_val;
}
