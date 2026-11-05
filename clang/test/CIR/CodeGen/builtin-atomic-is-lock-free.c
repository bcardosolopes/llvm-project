// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test __atomic_is_lock_free and __c11_atomic_is_lock_free builtins.
// For naturally-aligned types (int, etc.), the compiler folds these to
// constant true. For large types, a runtime call is emitted.

typedef _Bool bool;

struct Large { char data[128]; };

// The compiler constant-folds this case since int (4 bytes) is always
// lock-free on x86_64.
bool test_atomic_is_lock_free_int(int *p) {
  return __atomic_is_lock_free(sizeof(int), p);
}

// CIR-LABEL: @test_atomic_is_lock_free_int
// CIR: cir.const #true

// LLVM-LABEL: @test_atomic_is_lock_free_int
// LLVM-NOT: call{{.*}}__atomic_is_lock_free

// OGCG-LABEL: @test_atomic_is_lock_free_int
// OGCG-NOT: call{{.*}}__atomic_is_lock_free

// Large struct requires a runtime call.
bool test_atomic_is_lock_free_large(struct Large *p) {
  return __atomic_is_lock_free(sizeof(struct Large), p);
}

// CIR-LABEL: @test_atomic_is_lock_free_large
// CIR: cir.call @__atomic_is_lock_free({{%.*}}, {{%.*}})

// LLVM-LABEL: @test_atomic_is_lock_free_large
// LLVM: call zeroext i1 @__atomic_is_lock_free(i64 noundef 128,

// OGCG-LABEL: @test_atomic_is_lock_free_large
// OGCG: call{{.*}}@__atomic_is_lock_free(i64{{.*}}128,

// __c11_atomic_is_lock_free with a constant-foldable size.
bool test_c11_atomic_is_lock_free(void) {
  return __c11_atomic_is_lock_free(sizeof(int));
}

// CIR-LABEL: @test_c11_atomic_is_lock_free
// CIR: cir.const #true

// LLVM-LABEL: @test_c11_atomic_is_lock_free
// LLVM-NOT: call{{.*}}__atomic_is_lock_free

// OGCG-LABEL: @test_c11_atomic_is_lock_free
// OGCG-NOT: call{{.*}}__atomic_is_lock_free
