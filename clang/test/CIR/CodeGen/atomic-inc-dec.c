// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll --check-prefix=OGCG %s

_Atomic int x;
_Atomic(_Bool) b;

void test_postinc(void) {
  x++;
  b++;
}

void test_preinc(void) {
  ++x;
  ++b;
}

void test_postdec(void) {
  x--;
  b--;
}

void test_predec(void) {
  --x;
  --b;
}

// CIR-LABEL: @test_postinc
// CIR: cir.atomic.fetch add seq_cst fetch_first %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i) -> !s32i
// CIR: cir.atomic.xchg seq_cst %{{.*}}, %{{.*}} : (!cir.ptr<!u8i>, !u8i) -> !u8i

// CIR-LABEL: @test_preinc
// CIR: cir.atomic.fetch add seq_cst %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i) -> !s32i
// CIR: cir.store {{.*}} atomic(seq_cst) %{{.*}}, %{{.*}} : !cir.bool, !cir.ptr<!cir.bool>

// Bool decrement uses a CAS loop to normalize through i1 truncation,
// matching OGCG behavior. atomicrmw sub is wrong for _Bool because
// 0 - 1 = 0xFF which is not a valid bool value.
// CIR-LABEL: @test_postdec
// CIR: cir.atomic.fetch sub seq_cst fetch_first %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i) -> !s32i
// CIR: cir.atomic.cmpxchg success(seq_cst) failure(seq_cst) %{{.*}}, %{{.*}}, %{{.*}}

// CIR-LABEL: @test_predec
// CIR: cir.atomic.fetch sub seq_cst %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i) -> !s32i
// CIR: cir.atomic.cmpxchg success(seq_cst) failure(seq_cst) %{{.*}}, %{{.*}}, %{{.*}}

// LLVM-LABEL: @test_postinc
// LLVM: atomicrmw add ptr @x, i32 1 seq_cst
// LLVM: atomicrmw xchg ptr @b, i8 1 seq_cst

// LLVM-LABEL: @test_preinc
// LLVM: atomicrmw add ptr @x, i32 1 seq_cst
// LLVM: store atomic i8 1, ptr @b seq_cst

// LLVM-LABEL: @test_postdec
// LLVM: atomicrmw sub ptr @x, i32 1 seq_cst
// LLVM: cmpxchg ptr @b

// LLVM-LABEL: @test_predec
// LLVM: atomicrmw sub ptr @x, i32 1 seq_cst
// LLVM: cmpxchg ptr @b

// OGCG uses a CAS loop for _Bool dec, matching CIR.
// OGCG-LABEL: @test_postdec
// OGCG: atomicrmw sub ptr @x, i32 1 seq_cst
// OGCG: cmpxchg ptr @b

// OGCG-LABEL: @test_predec
// OGCG: atomicrmw sub ptr @x, i32 1 seq_cst
// OGCG: cmpxchg ptr @b
