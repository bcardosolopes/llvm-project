// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fblocks -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

int call_block(int (^a0)()) {
  return a0(1, 2, 3);
}

// LLVM-LABEL: define{{.*}} i32 @call_block(
// LLVM: [[BLOCK_ADDR:%.*]] = alloca ptr, i64 1, align 8
// LLVM: [[BLOCK:%.*]] = load ptr, ptr [[BLOCK_ADDR]], align 8
// LLVM: [[INVOKE_SLOT:%.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr }, ptr [[BLOCK]], i32 0, i32 3
// LLVM: [[RAW:%.*]] = load ptr, ptr [[INVOKE_SLOT]], align 8
// LLVM: [[RES:%.*]] = call i32 [[RAW]](ptr noundef [[BLOCK]], i32 noundef 1, i32 noundef 2, i32 noundef 3)
// LLVM: ret i32 {{%.*}}

// OGCG-LABEL: define{{.*}} i32 @call_block(
// OGCG: [[BLOCK:%.*]] = load ptr, ptr
// OGCG: [[INVOKE_SLOT:%.*]] = getelementptr inbounds nuw %struct.__block_literal_generic, ptr [[BLOCK]], i32 0, i32 3
// OGCG: [[RAW:%.*]] = load ptr, ptr [[INVOKE_SLOT]], align 8
// OGCG: [[RES:%.*]] = call i32 (ptr, i32, i32, i32, ...) [[RAW]](ptr noundef [[BLOCK]], i32 noundef 1, i32 noundef 2, i32 noundef 3)
// OGCG: ret i32 [[RES]]
