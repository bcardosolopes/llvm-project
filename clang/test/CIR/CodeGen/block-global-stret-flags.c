// RUN: %clang_cc1 -triple x86_64-apple-darwin9 -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM-X86
// RUN: %clang_cc1 -triple x86_64-apple-darwin9 -fblocks -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG-X86
// RUN: %clang_cc1 -triple arm64-apple-darwin -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM-ARM64
// RUN: %clang_cc1 -triple arm64-apple-darwin -fblocks -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG-ARM64

typedef struct bigbig {
  int array[512];
  char more[32];
} BigStruct_t;

void use(BigStruct_t (^)(int));

void test_global(void) {
  use(^(int param) {
    BigStruct_t result = {};
    result.array[23] = param;
    return result;
  });
}

// LLVM-X86: @__test_global_block_invoke.literal = internal constant { ptr, i32, i32, ptr, ptr } { ptr @_NSConcreteGlobalBlock, i32 1879048192, i32 0, ptr @__test_global_block_invoke.invoke, ptr @__test_global_block_invoke.descriptor }, align 8
// LLVM-X86-LABEL: define internal void @__test_global_block_invoke.invoke(ptr dead_on_unwind noalias writable sret(%struct.bigbig) align 4 %{{.*}}, ptr noundef %{{.*}}, i32 noundef %{{.*}})
// LLVM-X86-LABEL: define void @test_global()

// OGCG-X86: @__block_literal_global = internal constant { ptr, i32, i32, ptr, ptr } { ptr @_NSConcreteGlobalBlock, i32 1879048192, i32 0, ptr @__test_global_block_invoke, ptr @__block_descriptor_tmp }, align 8
// OGCG-X86-LABEL: define void @test_global()
// OGCG-X86-LABEL: define internal void @__test_global_block_invoke(ptr dead_on_unwind noalias writable sret(%struct.bigbig) align 4 %{{.*}}, ptr noundef %{{.*}}, i32 noundef %{{.*}})

// LLVM-ARM64: @__test_global_block_invoke.literal = internal constant { ptr, i32, i32, ptr, ptr } { ptr @_NSConcreteGlobalBlock, i32 1342177280, i32 0, ptr @__test_global_block_invoke.invoke, ptr @__test_global_block_invoke.descriptor }, align 8
// LLVM-ARM64-LABEL: define internal void @__test_global_block_invoke.invoke(ptr dead_on_unwind noalias writable sret(%struct.bigbig) align 4 %{{.*}}, ptr noundef %{{.*}}, i32 noundef %{{.*}})
// LLVM-ARM64-LABEL: define void @test_global()

// OGCG-ARM64: @__block_literal_global = internal constant { ptr, i32, i32, ptr, ptr } { ptr @_NSConcreteGlobalBlock, i32 1342177280, i32 0, ptr @__test_global_block_invoke, ptr @__block_descriptor_tmp }, align 8
// OGCG-ARM64-LABEL: define void @test_global()
// OGCG-ARM64-LABEL: define internal void @__test_global_block_invoke(ptr dead_on_unwind noalias writable sret(%struct.bigbig) align 4 %{{.*}}, ptr noundef %{{.*}}, i32 noundef %{{.*}})
