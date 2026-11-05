// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Test that non-fundamental _BitInt types used as array subscript indices
// are properly cast to a fundamental integer type before GetElementOp and
// PtrStrideOp.

void test_signed_bitint_subscript(int *arr, _BitInt(15) idx) {
  arr[idx] = 42;
}

// CIR-LABEL: @test_signed_bitint_subscript
// CIR:         %[[STORED:.*]] = cir.cast integral %arg1 : !cir.int<s, 15> -> !s16i
// CIR:         cir.store{{.*}} %[[STORED]], {{.*}} !s16i, !cir.ptr<!s16i>
// CIR:         %[[RAW:.*]] = cir.load {{.*}} !cir.ptr<!s16i>, !s16i
// CIR:         %[[IDX:.*]] = cir.cast integral %[[RAW]] : !s16i -> !cir.int<s, 15>
// CIR:         %[[CAST:.*]] = cir.cast integral %[[IDX]] : !cir.int<s, 15> -> !s64i
// CIR:         cir.ptr_stride inbounds {{.*}}, %[[CAST]] : (!cir.ptr<!s32i>, !s64i) -> !cir.ptr<!s32i>

// LLVM-LABEL: @test_signed_bitint_subscript
// LLVM:         %[[STORED:.*]] = sext i15 %1 to i16
// LLVM:         store i16 %[[STORED]]
// LLVM:         %[[RAW:.*]] = load i16
// LLVM:         %[[IDX:.*]] = trunc i16 %[[RAW]] to i15
// LLVM:         %[[SEXT:.*]] = sext i15 %[[IDX]] to i64
// LLVM:         getelementptr inbounds i32, ptr %{{.*}}, i64 %[[SEXT]]

void test_unsigned_bitint_subscript(int *arr, unsigned _BitInt(7) idx) {
  arr[idx] = 99;
}

// CIR-LABEL: @test_unsigned_bitint_subscript
// CIR:         %[[STORED:.*]] = cir.cast integral %arg1 : !cir.int<u, 7> -> !u8i
// CIR:         cir.store{{.*}} %[[STORED]], {{.*}} !u8i, !cir.ptr<!u8i>
// CIR:         %[[RAW:.*]] = cir.load {{.*}} !cir.ptr<!u8i>, !u8i
// CIR:         %[[IDX:.*]] = cir.cast integral %[[RAW]] : !u8i -> !cir.int<u, 7>
// CIR:         %[[CAST:.*]] = cir.cast integral %[[IDX]] : !cir.int<u, 7> -> !s64i
// CIR:         cir.ptr_stride inbounds|nuw {{.*}}, %[[CAST]] : (!cir.ptr<!s32i>, !s64i) -> !cir.ptr<!s32i>

// LLVM-LABEL: @test_unsigned_bitint_subscript
// LLVM:         %[[STORED:.*]] = zext i7 %1 to i8
// LLVM:         store i8 %[[STORED]]
// LLVM:         %[[RAW:.*]] = load i8
// LLVM:         %[[IDX:.*]] = trunc i8 %[[RAW]] to i7
// LLVM:         %[[ZEXT:.*]] = zext i7 %[[IDX]] to i64
// LLVM:         getelementptr inbounds nuw i32, ptr %{{.*}}, i64 %[[ZEXT]]

void test_bitint_local_array(_BitInt(15) idx) {
  int arr[10];
  arr[idx] = 42;
}

// CIR-LABEL: @test_bitint_local_array
// CIR:         %[[RAW:.*]] = cir.load {{.*}} !cir.ptr<!s16i>, !s16i
// CIR:         %[[IDX:.*]] = cir.cast integral %[[RAW]] : !s16i -> !cir.int<s, 15>
// CIR:         %[[CAST:.*]] = cir.cast integral %[[IDX]] : !cir.int<s, 15> -> !s64i
// CIR:         cir.get_element {{.*}}[%[[CAST]] : !s64i]

// LLVM-LABEL: @test_bitint_local_array
// LLVM:         %[[RAW:.*]] = load i16
// LLVM:         %[[IDX:.*]] = trunc i16 %[[RAW]] to i15
// LLVM:         %[[SEXT:.*]] = sext i15 %[[IDX]] to i64
// LLVM:         getelementptr inbounds [10 x i32], ptr %{{.*}}, i32 0, i64 %[[SEXT]]

// Test pointer arithmetic with _BitInt index (p + n).
int *test_bitint_ptr_arith(int *p, _BitInt(9) n) {
  return p + n;
}

// CIR-LABEL: @test_bitint_ptr_arith
// CIR:         %[[RAW:.*]] = cir.load {{.*}} !cir.ptr<!s16i>, !s16i
// CIR:         %[[N:.*]] = cir.cast integral %[[RAW]] : !s16i -> !cir.int<s, 9>
// CIR:         %[[CAST:.*]] = cir.cast integral %[[N]] : !cir.int<s, 9> -> !s64i
// CIR:         cir.ptr_stride inbounds {{.*}}, %[[CAST]]

// LLVM-LABEL: @test_bitint_ptr_arith
// LLVM:         %[[RAW:.*]] = load i16
// LLVM:         %[[N:.*]] = trunc i16 %[[RAW]] to i9
// LLVM:         %[[SEXT:.*]] = sext i9 %[[N]] to i64
// LLVM:         getelementptr inbounds i32, ptr %{{.*}}, i64 %[[SEXT]]
