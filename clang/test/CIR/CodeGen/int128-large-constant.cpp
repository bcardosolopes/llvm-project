// RUN: %clang_cc1 -std=c++17 -triple aarch64-none-linux-android21 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple aarch64-none-linux-android21 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Test that __int128 constants larger than 64 bits are handled correctly.
// Previously crashed in CIRGenBuilder::getConstInt due to narrowing APSInt
// to int64_t/uint64_t via getSExtValue()/getZExtValue().

const unsigned __int128 big_const = ((unsigned __int128)1 << 100) | 42;

unsigned __int128 get_big() {
  return big_const;
}

// CHECK: cir.const #cir.int<1267650600228229401496703205418> : !u128i
// LLVM: i128 1267650600228229401496703205418
