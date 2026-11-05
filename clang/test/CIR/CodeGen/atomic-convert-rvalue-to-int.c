// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll --check-prefix=OGCG %s

// Test that _Atomic stores of register-sized struct types work correctly.
// This exercises AtomicInfo::convertRValueToInt when shouldCastToInt returns
// true for non-floating-point types (memory coercion path).

struct Pair { short a, b; };

void store_atomic_struct(_Atomic(struct Pair) *p, struct Pair val) {
  // The store goes through convertRValueToInt, which uses the memory
  // coercion path (alloca + store + load-as-int) for struct types.
  //
  // CIR-LABEL: @store_atomic_struct
  // CIR: cir.store align(4) syncscope(system) atomic(seq_cst)
  //
  // LLVM-LABEL: @store_atomic_struct
  // LLVM: store atomic i32 %{{.*}}, ptr %{{.*}} seq_cst, align 4
  //
  // OGCG-LABEL: @store_atomic_struct
  // OGCG: store atomic i32 %{{.*}}, ptr %{{.*}} seq_cst, align 4
  __c11_atomic_store(p, val, __ATOMIC_SEQ_CST);
}
