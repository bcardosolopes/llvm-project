// RUN: %clang_cc1 -triple x86_64-apple-darwin9 -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM-X86
// RUN: %clang_cc1 -triple x86_64-apple-darwin9 -fblocks -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG-X86
// RUN: %clang_cc1 -triple arm64-apple-darwin -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM-ARM64
// RUN: %clang_cc1 -triple arm64-apple-darwin -fblocks -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG-ARM64

typedef struct bigbig {
  int array[512];
  char more[32];
} BigStruct_t;

void use(BigStruct_t (^)(int));

void test(int argc) {
  BigStruct_t (^local)(int) = ^(int param) {
    BigStruct_t result = {};
    result.array[23] = argc + param;
    return result;
  };
  use(local);
}

// LLVM-X86-LABEL: define internal void @__test_block_invoke.invoke(ptr dead_on_unwind noalias writable sret(%struct.bigbig) align 4 %{{.*}}, ptr noundef %{{.*}}, i32 noundef %{{.*}})
// LLVM-X86-LABEL: define void @test(i32 noundef %{{.*}})
// LLVM-X86: store i32 1610612736, ptr %{{.*}}, align 8

// OGCG-X86-LABEL: define void @test(i32 noundef %{{.*}})
// OGCG-X86: store i32 1610612736, ptr %{{.*}}, align 8
// OGCG-X86-LABEL: define internal void @__test_block_invoke(ptr dead_on_unwind noalias writable sret(%struct.bigbig) align 4 %{{.*}}, ptr noundef %{{.*}}, i32 noundef %{{.*}})

// LLVM-ARM64-LABEL: define internal void @__test_block_invoke.invoke(ptr dead_on_unwind noalias writable sret(%struct.bigbig) align 4 %{{.*}}, ptr noundef %{{.*}}, i32 noundef %{{.*}})
// LLVM-ARM64-LABEL: define void @test(i32 noundef %{{.*}})
// LLVM-ARM64: store i32 1073741824, ptr %{{.*}}, align 8

// OGCG-ARM64-LABEL: define void @test(i32 noundef %{{.*}})
// OGCG-ARM64: store i32 1073741824, ptr %{{.*}}, align 8
// OGCG-ARM64-LABEL: define internal void @__test_block_invoke(ptr dead_on_unwind noalias writable sret(%struct.bigbig) align 4 %{{.*}}, ptr noundef %{{.*}}, i32 noundef %{{.*}})
