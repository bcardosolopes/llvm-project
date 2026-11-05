// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s < %t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fno-clangir-call-conv-lowering %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll

// Test that atomic operations on types larger than 16 bytes emit
// library calls to __atomic_* runtime functions.
//
//

#include <stdatomic.h>

typedef struct {
  long a, b, c;  // 24 bytes - exceeds 16 byte threshold
} BigStruct;

_Atomic(BigStruct) global_big;

BigStruct load_big(void) {
  return atomic_load(&global_big);
}

// CHECK: cir.func private @__atomic_load
// CHECK: cir.func {{.*}} @load_big
// CHECK:   %[[SIZE:.*]] = cir.const #cir.int<24> : !u64i
// CHECK:   cir.call @__atomic_load(%[[SIZE]], {{.*}}, {{.*}}, {{.*}})

// LLVM: define {{.*}} @load_big
// LLVM:   call void @__atomic_load(i64 noundef 24,

void store_big(BigStruct val) {
  atomic_store(&global_big, val);
}

// CHECK: cir.func {{.*}} @store_big
// CHECK:   cir.call @__atomic_store({{.*}})

// LLVM: define {{.*}} @store_big
// LLVM:   call void @__atomic_store(i64 noundef 24,
