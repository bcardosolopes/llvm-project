// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

void *__attribute__((alloc_align(1))) alloc(int align);

// Constant non-power-of-2: both CIR and OGCG emit llvm.assume.
// CIR-LABEL: @t_const_non_pow2
// CIR:         %[[CALL:.*]] = cir.call @alloc
// CIR:         cir.assume_aligned %[[CALL]] alignment 7 : !cir.ptr<!void>

// LLVM-LABEL: @t_const_non_pow2
// LLVM:         %[[CALL:.*]] = call ptr @alloc(i32 noundef 7)
// LLVM-NEXT:    call void @llvm.assume(i1 true) [ "align"(ptr %[[CALL]], i64 7) ]

// OGCG-LABEL: @t_const_non_pow2
// OGCG:         %[[CALL:.*]] = call ptr @alloc(i32 noundef 7)
// OGCG-NEXT:    call void @llvm.assume(i1 true) [ "align"(ptr %[[CALL]], i64 7) ]
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnon-power-of-two-alignment"
void t_const_non_pow2(void) {
  alloc(7);
}
#pragma clang diagnostic pop

// Constant power-of-2: both CIR and OGCG emit 'align 8' on call.
// CIR-LABEL: @t_const_pow2
// CIR:         %[[CALL:.*]] = cir.call @alloc
// CIR:         cir.assume_aligned %[[CALL]] alignment 8 : !cir.ptr<!void>

// LLVM-LABEL: @t_const_pow2
// LLVM:         %{{.*}} = call align 8 ptr @alloc(i32 noundef 8)

// OGCG-LABEL: @t_const_pow2
// OGCG:         %{{.*}} = call align 8 ptr @alloc(i32 noundef 8)
void t_const_pow2(void) {
  alloc(8);
}
