// RUN: %clang_cc1 -emit-llvm -fclangir -triple x86_64-apple-darwin10 %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -emit-llvm -triple x86_64-apple-darwin10 %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// Verify byval arguments don't generate unnecessary memcpy
// when the source already has sufficient alignment.

struct AlignedS {
  long a, b, c;
};
struct UnalignedS {
  int a, b, c, d, e, f, g, h, i, j, k, l;
};

// Test 1: Pointer dereference passed via byval — the source has natural
// alignment >= byval requirement, so no memcpy is needed.
void callee_aligned(struct AlignedS);
// LLVM-LABEL: define{{.*}} void @test_no_copy(
// LLVM-NOT:     memcpy
// LLVM:         call void @callee_aligned(
// LLVM-NEXT:    ret void
//
// OGCG-LABEL: define{{.*}} void @test_no_copy(
// OGCG-NOT:     memcpy
// OGCG:         call void @callee_aligned(
// OGCG-NEXT:    ret void
void test_no_copy(struct AlignedS *p) {
  callee_aligned(*p);
}

// Test 2: Pointer dereference where the struct's natural alignment (4) is
// less than the byval requirement (8) — a memcpy IS needed.
void callee_unaligned(struct UnalignedS);
// LLVM-LABEL: define{{.*}} void @test_needs_copy(
// LLVM:         alloca %struct.UnalignedS, {{.*}}align 8
// LLVM:         memcpy
// LLVM:         call void @callee_unaligned(
//
// OGCG-LABEL: define{{.*}} void @test_needs_copy(
// OGCG:         alloca %struct.UnalignedS, align 8
// OGCG:         memcpy
// OGCG:         call void @callee_unaligned(
void test_needs_copy(struct UnalignedS *q) {
  callee_unaligned(*q);
}

void clobber(void);
// Test 3: A local stack slot can be realigned in place to satisfy a byval
// requirement, so no extra temp+memcpy should be introduced.
//
// LLVM-LABEL: define{{.*}} void @test_stack_lvalue(
// LLVM:         [[LOCAL:%.*]] = alloca %struct.UnalignedS, {{.*}}align 8
// LLVM-NOT:     memcpy
// LLVM:         call void @clobber(
// LLVM-NOT:     memcpy
// LLVM:         call void @callee_unaligned(ptr noundef byval(%struct.UnalignedS) align 8 [[LOCAL]])
//
// OGCG-LABEL: define{{.*}} void @test_stack_lvalue(
// OGCG:         [[LOCAL:%.*]] = alloca %struct.UnalignedS, {{.*}}align 8
// OGCG-NOT:     memcpy
// OGCG:         call void @clobber(
// OGCG-NOT:     memcpy
// OGCG:         call void @callee_unaligned(ptr noundef byval(%struct.UnalignedS) align 8 [[LOCAL]])
void test_stack_lvalue(void) {
  struct UnalignedS local;
  clobber();
  callee_unaligned(local);
}
