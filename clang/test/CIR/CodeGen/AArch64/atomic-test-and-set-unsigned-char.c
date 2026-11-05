// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Regression test: the ODS verifier for cir.atomic.test_and_set and
// cir.atomic.clear previously only accepted !cir.ptr<!s8i> (signed i8),
// rejecting !cir.ptr<!u8i>. The verifier now accepts both signed and unsigned
// 8-bit integer pointers. The codegen currently bitcasts unsigned char pointers
// to signed before the atomic op, but the verifier should accept both forms.

unsigned char flag;

void test_and_set(void) {
  __atomic_test_and_set(&flag, __ATOMIC_SEQ_CST);
}

// CHECK-LABEL: @test_and_set
// CHECK: cir.get_global @flag : !cir.ptr<!u8i>
// CHECK: cir.cast bitcast {{%.*}} : !cir.ptr<!u8i> -> !cir.ptr<!s8i>
// CHECK: cir.atomic.test_and_set seq_cst {{%.*}} : !cir.ptr<!s8i> -> !cir.bool

// LLVM-LABEL: @test_and_set
// LLVM: atomicrmw xchg ptr @flag, i8 1 seq_cst

void clear(void) {
  __atomic_clear(&flag, __ATOMIC_SEQ_CST);
}

// CHECK-LABEL: @clear
// CHECK: cir.get_global @flag : !cir.ptr<!u8i>
// CHECK: cir.cast bitcast {{%.*}} : !cir.ptr<!u8i> -> !cir.ptr<!s8i>
// CHECK: cir.atomic.clear seq_cst {{%.*}} : !cir.ptr<!s8i>

// LLVM-LABEL: @clear
// LLVM: store atomic i8 0, ptr @flag seq_cst
