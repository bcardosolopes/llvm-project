// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Test dynamic __builtin_offsetof with runtime array index.

struct S {
  unsigned char prefix;
  char data[104];
};

// CIR-LABEL: @dynamic_offsetof
// CIR:   %[[INIT:.*]] = cir.const #cir.int<0> : !u64i
// CIR:   %[[FIELD:.*]] = cir.const #cir.int<1> : !u64i
// CIR:   %[[BASE:.*]] = cir.binop(add, %[[INIT]], %[[FIELD]]) : !u64i
// CIR:   %[[IDX:.*]] = cir.binop(add,
// CIR:   %[[CAST:.*]] = cir.cast integral %[[IDX]] : !s32i -> !u64i
// CIR:   %[[ELEM:.*]] = cir.const #cir.int<1> : !u64i
// CIR:   %[[MUL:.*]] = cir.binop(mul, %[[CAST]], %[[ELEM]]) : !u64i
// CIR:   %[[RESULT:.*]] = cir.binop(add, %[[BASE]], %[[MUL]]) : !u64i
int dynamic_offsetof(int len) {
  return __builtin_offsetof(struct S, data[len + 1]);
}

// LLVM-LABEL: @dynamic_offsetof
// LLVM:   %[[IDX:.*]] = add nsw i32
// LLVM:   %[[EXT:.*]] = sext i32 %[[IDX]] to i64
// LLVM:   %[[MUL:.*]] = mul i64 %[[EXT]], 1
// LLVM:   %[[ADD:.*]] = add i64 1, %[[MUL]]
// LLVM:   %[[TRUNC:.*]] = trunc i64 %[[ADD]] to i32

// Constant offsetof still works (folds to constant).
// CIR-LABEL: @const_offsetof
// CIR:   %[[C:.*]] = cir.const #cir.int<1> : !s64i
long const_offsetof(void) {
  return __builtin_offsetof(struct S, data);
}
