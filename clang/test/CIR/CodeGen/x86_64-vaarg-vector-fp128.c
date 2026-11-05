// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef __builtin_va_list va_list;
typedef float v4f __attribute__((vector_size(16)));

v4f test_vec(va_list ap) {
  return __builtin_va_arg(ap, v4f);
}

// LLVM-LABEL: define dso_local <4 x float> @test_vec(
// LLVM-NOT: va_arg
// LLVM: %[[FP_OFFSET_P:.*]] = getelementptr inbounds {{.*}}, i32 0, i32 1
// LLVM: %[[FP_OFFSET:.*]] = load i32, ptr %[[FP_OFFSET_P]], align 4
// LLVM: %[[FITS:.*]] = icmp ule i32 %[[FP_OFFSET]], 160
// LLVM: %[[REG_SAVE_PTR:.*]] = getelementptr inbounds {{.*}}, i32 0, i32 3
// LLVM: %[[REG_SAVE:.*]] = load ptr, ptr %[[REG_SAVE_PTR]], align 8
// LLVM: %[[FP_OFFSET_I64:.*]] = zext i32 %[[FP_OFFSET]] to i64
// LLVM: %[[REG_ADDR:.*]] = getelementptr inbounds i8, ptr %[[REG_SAVE]], i64 %[[FP_OFFSET_I64]]
// LLVM: %[[NEW_FP_OFFSET:.*]] = add i32 %[[FP_OFFSET]], 16
// LLVM: store i32 %[[NEW_FP_OFFSET]], ptr %[[FP_OFFSET_P]], align 4
// LLVM: %[[OVERFLOW_PTR:.*]] = getelementptr inbounds {{.*}}, i32 0, i32 2
// LLVM: %[[OVERFLOW:.*]] = load ptr, ptr %[[OVERFLOW_PTR]], align 8
// LLVM: %[[ROUND_UP:.*]] = getelementptr inbounds i8, ptr %[[OVERFLOW]], i64 15
// LLVM: %[[ALIGNED:.*]] = call ptr @llvm.ptrmask.p0.i64(ptr %[[ROUND_UP]], i64 -16)
// LLVM: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[ALIGNED]], i64 16
// LLVM: store ptr %[[NEXT]], ptr %[[OVERFLOW_PTR]], align 8
// LLVM: load <4 x float>, ptr %{{.*}}, align 16

// OGCG-LABEL: define dso_local <4 x float> @test_vec(
// OGCG-NOT: va_arg
// OGCG: %[[FP_OFFSET_P:.*]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %{{.*}}, i32 0, i32 1
// OGCG: %[[FP_OFFSET:.*]] = load i32, ptr %[[FP_OFFSET_P]], align 4
// OGCG: %[[FITS:.*]] = icmp ule i32 %[[FP_OFFSET]], 160
// OGCG: %[[REG_SAVE_PTR:.*]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %{{.*}}, i32 0, i32 3
// OGCG: %[[REG_SAVE:.*]] = load ptr, ptr %[[REG_SAVE_PTR]], align 8
// OGCG: %[[REG_ADDR:.*]] = getelementptr i8, ptr %[[REG_SAVE]], i32 %[[FP_OFFSET]]
// OGCG: %[[OVERFLOW_PTR:.*]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %{{.*}}, i32 0, i32 2
// OGCG: %[[OVERFLOW:.*]] = load ptr, ptr %[[OVERFLOW_PTR]], align 8
// OGCG: %[[ROUND_UP:.*]] = getelementptr inbounds i8, ptr %[[OVERFLOW]], i32 15
// OGCG: %[[ALIGNED:.*]] = call ptr @llvm.ptrmask.p0.i64(ptr %[[ROUND_UP]], i64 -16)
// OGCG: load <4 x float>, ptr %{{.*}}, align 16

__float128 test_fp128(va_list ap) {
  return __builtin_va_arg(ap, __float128);
}

// LLVM-LABEL: define dso_local fp128 @test_fp128(
// LLVM-NOT: va_arg
// LLVM: %[[FP_OFFSET_P:.*]] = getelementptr inbounds {{.*}}, i32 0, i32 1
// LLVM: %[[FP_OFFSET:.*]] = load i32, ptr %[[FP_OFFSET_P]], align 4
// LLVM: %[[FITS:.*]] = icmp ule i32 %[[FP_OFFSET]], 160
// LLVM: %[[REG_SAVE_PTR:.*]] = getelementptr inbounds {{.*}}, i32 0, i32 3
// LLVM: %[[REG_SAVE:.*]] = load ptr, ptr %[[REG_SAVE_PTR]], align 8
// LLVM: %[[FP_OFFSET_I64:.*]] = zext i32 %[[FP_OFFSET]] to i64
// LLVM: %[[REG_ADDR:.*]] = getelementptr inbounds i8, ptr %[[REG_SAVE]], i64 %[[FP_OFFSET_I64]]
// LLVM: %[[NEW_FP_OFFSET:.*]] = add i32 %[[FP_OFFSET]], 16
// LLVM: store i32 %[[NEW_FP_OFFSET]], ptr %[[FP_OFFSET_P]], align 4
// LLVM: %[[OVERFLOW_PTR:.*]] = getelementptr inbounds {{.*}}, i32 0, i32 2
// LLVM: %[[OVERFLOW:.*]] = load ptr, ptr %[[OVERFLOW_PTR]], align 8
// LLVM: %[[ROUND_UP:.*]] = getelementptr inbounds i8, ptr %[[OVERFLOW]], i64 15
// LLVM: %[[ALIGNED:.*]] = call ptr @llvm.ptrmask.p0.i64(ptr %[[ROUND_UP]], i64 -16)
// LLVM: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[ALIGNED]], i64 16
// LLVM: store ptr %[[NEXT]], ptr %[[OVERFLOW_PTR]], align 8
// LLVM: load fp128, ptr %{{.*}}, align 16

// OGCG-LABEL: define dso_local fp128 @test_fp128(
// OGCG-NOT: va_arg
// OGCG: %[[FP_OFFSET_P:.*]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %{{.*}}, i32 0, i32 1
// OGCG: %[[FP_OFFSET:.*]] = load i32, ptr %[[FP_OFFSET_P]], align 4
// OGCG: %[[FITS:.*]] = icmp ule i32 %[[FP_OFFSET]], 160
// OGCG: %[[REG_SAVE_PTR:.*]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %{{.*}}, i32 0, i32 3
// OGCG: %[[REG_SAVE:.*]] = load ptr, ptr %[[REG_SAVE_PTR]], align 8
// OGCG: %[[REG_ADDR:.*]] = getelementptr i8, ptr %[[REG_SAVE]], i32 %[[FP_OFFSET]]
// OGCG: %[[OVERFLOW_PTR:.*]] = getelementptr inbounds nuw %struct.__va_list_tag, ptr %{{.*}}, i32 0, i32 2
// OGCG: %[[OVERFLOW:.*]] = load ptr, ptr %[[OVERFLOW_PTR]], align 8
// OGCG: %[[ROUND_UP:.*]] = getelementptr inbounds i8, ptr %[[OVERFLOW]], i32 15
// OGCG: %[[ALIGNED:.*]] = call ptr @llvm.ptrmask.p0.i64(ptr %[[ROUND_UP]], i64 -16)
// OGCG: load fp128, ptr %{{.*}}, align 16
