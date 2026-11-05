// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test integer-valued type traits (TypeTraitExpr stored as int, not bool).
// constexpr variables are constant-folded at use site, so no globals are emitted.

constexpr auto r0 = __array_rank(int);
constexpr auto r1 = __array_rank(int[3]);
constexpr auto r2 = __array_rank(int[3][4]);

int test() {
    return r0 + r1 + r2;
}

// CIR-LABEL: @_Z4testv
// CIR: cir.const #cir.int<0> : !u64i
// CIR: cir.const #cir.int<1> : !u64i
// CIR: cir.binop(add,
// CIR: cir.const #cir.int<2> : !u64i
// CIR: cir.binop(add,

// LLVM-LABEL: @_Z4testv
// LLVM: ret i32 3
