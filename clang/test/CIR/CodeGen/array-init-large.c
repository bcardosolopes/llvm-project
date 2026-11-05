// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: large arrays (> UINT32_MAX) with partial initializers must preserve
// the non-zero initial elements. Previously CIR truncated the array bound
// to 32 bits, dropping the initializer entirely.

char big_char[4294967296] = {1};

// CIR: cir.global {{.*}} @big_char = #cir.const_array<[#cir.int<1> : !s8i], trailing_zeros> : !cir.array<!s8i x 4294967296>

// LLVM: @big_char ={{.*}} global <{ i8, [4294967295 x i8] }> <{ i8 1, [4294967295 x i8] zeroinitializer }>

// OGCG: @big_char ={{.*}} global <{ i8, [4294967295 x i8] }> <{ i8 1, [4294967295 x i8] zeroinitializer }>
