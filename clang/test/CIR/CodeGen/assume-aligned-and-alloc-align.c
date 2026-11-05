// Test that CIR correctly handles both assume_aligned and alloc_align attributes
// on the same function. When both provide constant alignments, the maximum is
// used (matching OGCG's maybeRaiseRetAlignmentAttribute). When alloc_align has
// a runtime value, CIR emits llvm.assume with the "align" operand bundle.
//
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

void *my_aligned_alloc(int size, int alignment) __attribute__((assume_aligned(32), alloc_align(2)));

// alloc_align(16) < assume_aligned(32) → use assume_aligned(32)
// CIR-LABEL: @t0_immediate0
// CIR: %[[CALL:.*]] = cir.call @my_aligned_alloc
// CIR: cir.assume_aligned %[[CALL]] alignment 32
// LLVM-LABEL: @t0_immediate0
// LLVM: call align 32 ptr @my_aligned_alloc(i32 noundef 320, i32 noundef 16)
// OGCG-LABEL: @t0_immediate0
// OGCG: call align 32 ptr @my_aligned_alloc(i32 noundef 320, i32 noundef 16)
void *t0_immediate0(void) {
  return my_aligned_alloc(320, 16);
}

// alloc_align(32) == assume_aligned(32) → use 32
// CIR-LABEL: @t1_immediate1
// CIR: %[[CALL:.*]] = cir.call @my_aligned_alloc
// CIR: cir.assume_aligned %[[CALL]] alignment 32
// LLVM-LABEL: @t1_immediate1
// LLVM: call align 32 ptr @my_aligned_alloc(i32 noundef 320, i32 noundef 32)
// OGCG-LABEL: @t1_immediate1
// OGCG: call align 32 ptr @my_aligned_alloc(i32 noundef 320, i32 noundef 32)
void *t1_immediate1(void) {
  return my_aligned_alloc(320, 32);
}

// alloc_align(64) > assume_aligned(32) → use alloc_align(64)
// CIR-LABEL: @t2_immediate2
// CIR: %[[CALL:.*]] = cir.call @my_aligned_alloc
// CIR: cir.assume_aligned %[[CALL]] alignment 64
// LLVM-LABEL: @t2_immediate2
// LLVM: call align 64 ptr @my_aligned_alloc(i32 noundef 320, i32 noundef 64)
// OGCG-LABEL: @t2_immediate2
// OGCG: call align 64 ptr @my_aligned_alloc(i32 noundef 320, i32 noundef 64)
void *t2_immediate2(void) {
  return my_aligned_alloc(320, 64);
}

// alloc_align(variable) → use assume_aligned(32) for call attr + llvm.assume for dynamic
// CIR-LABEL: @t3_variable
// CIR: %[[CALL:.*]] = cir.call @my_aligned_alloc
// CIR: %[[AA:.*]] = cir.assume_aligned %[[CALL]] alignment 32
// CIR: cir.assume_aligned %[[AA]] [align_value %{{.*}} : !s32i]
// LLVM-LABEL: @t3_variable
// LLVM: %[[CALL:.*]] = call align 32 ptr @my_aligned_alloc(i32 noundef 320, i32 noundef %{{.*}})
// LLVM: call void @llvm.assume(i1 true) [ "align"(ptr %[[CALL]], i64 %{{.*}}) ]
// OGCG-LABEL: @t3_variable
// OGCG: %[[CALL:.*]] = call align 32 ptr @my_aligned_alloc(i32 noundef 320, i32 noundef %{{.*}})
// OGCG: call void @llvm.assume(i1 true) [ "align"(ptr %[[CALL]], i64 %{{.*}}) ]
void *t3_variable(int alignment) {
  return my_aligned_alloc(320, alignment);
}
