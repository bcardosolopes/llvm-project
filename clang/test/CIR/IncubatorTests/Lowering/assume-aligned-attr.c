// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

void *m1(void) __attribute__((assume_aligned(64)));

// CIR-LABEL: @test_no_offset
// CIR:         %[[CALL:.*]] = cir.call @m1()
// CIR-NEXT:    %[[ALIGNED:.*]] = cir.assume_aligned %[[CALL]] alignment 64 : !cir.ptr<!void>
// CIR-NEXT:    cir.return %[[ALIGNED]] : !cir.ptr<!void>

// LLVM-LABEL: @test_no_offset
// LLVM:         %{{.*}} = call align 64 ptr @m1()

// OGCG-LABEL: @test_no_offset
// OGCG:         %{{.*}} = call align 64 ptr @m1()
void *test_no_offset(void) {
  return m1();
}

void *m2(void) __attribute__((assume_aligned(64, 12)));

// CIR-LABEL: @test_with_offset
// CIR:         %[[CALL:.*]] = cir.call @m2()
// CIR:         %[[OFFSET:.*]] = cir.const #cir.int<12>
// CIR-NEXT:    %[[ALIGNED:.*]] = cir.assume_aligned %[[CALL]] alignment 64 [offset %[[OFFSET]] : !s32i] : !cir.ptr<!void>
// CIR-NEXT:    cir.return %[[ALIGNED]] : !cir.ptr<!void>

// LLVM-LABEL: @test_with_offset
// LLVM:         %[[CALL:.*]] = call ptr @m2()
// LLVM-NEXT:    call void @llvm.assume(i1 true) [ "align"(ptr %[[CALL]], i64 64, i64 12) ]

// OGCG-LABEL: @test_with_offset
// OGCG:         %[[CALL:.*]] = call ptr @m2()
// OGCG-NEXT:    call void @llvm.assume(i1 true) [ "align"(ptr %[[CALL]], i64 64, i64 12) ]
void *test_with_offset(void) {
  return m2();
}
