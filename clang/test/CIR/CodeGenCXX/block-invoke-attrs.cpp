// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fblocks -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -std=c++17 -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=AARCH64

void sink(void (^)(void));
void side(void);

void capture(int x) {
  sink(^{ if (x) side(); });
}

void global_block(void) {
  sink(^{ side(); });
}

// CIR-LABEL: cir.func no_inline optnone internal private @___Z7capturei_block_invoke.invoke
// CIR-LABEL: cir.func no_inline optnone internal private @___Z12global_blockv_block_invoke.invoke

// LLVM: ; Function Attrs: noinline nounwind optnone
// LLVM-LABEL: define internal void @___Z7capturei_block_invoke.invoke(
// LLVM-SAME: ptr noundef %{{.*}}) #[[BLOCK_ATTR:[0-9]+]]
// LLVM: call void @_Z4sidev()
// LLVM: ; Function Attrs: mustprogress noinline nounwind optnone
// LLVM-LABEL: define dso_local void @_Z7capturei(
// LLVM: ; Function Attrs: noinline nounwind optnone
// LLVM-LABEL: define internal void @___Z12global_blockv_block_invoke.invoke(
// LLVM-SAME: ptr noundef %{{.*}}) #[[BLOCK_ATTR]]
// LLVM: call void @_Z4sidev()
// LLVM: ; Function Attrs: mustprogress noinline nounwind optnone
// LLVM-LABEL: define dso_local void @_Z12global_blockv(
// LLVM: attributes #[[BLOCK_ATTR]] = { {{.*}}noinline{{.*}}nounwind{{.*}}optnone{{.*}}"no-trapping-math"="true"{{.*}}"stack-protector-buffer-size"="8"{{.*}}"target-features"="+cx8,+mmx,+sse,+sse2,+x87"{{.*}} }

// AARCH64: ; Function Attrs: noinline nounwind optnone
// AARCH64-LABEL: define internal void @___Z7capturei_block_invoke.invoke(
// AARCH64-SAME: ptr noundef %{{.*}}) #[[BLOCK_ATTR:[0-9]+]]
// AARCH64: ; Function Attrs: mustprogress noinline nounwind optnone
// AARCH64-LABEL: define dso_local void @_Z7capturei(
// AARCH64: ; Function Attrs: noinline nounwind optnone
// AARCH64-LABEL: define internal void @___Z12global_blockv_block_invoke.invoke(
// AARCH64-SAME: ptr noundef %{{.*}}) #[[BLOCK_ATTR]]
// AARCH64: ; Function Attrs: mustprogress noinline nounwind optnone
// AARCH64-LABEL: define dso_local void @_Z12global_blockv(
// AARCH64: attributes #[[BLOCK_ATTR]] = { {{.*}}noinline{{.*}}nounwind{{.*}}optnone{{.*}}"no-trapping-math"="true"{{.*}}"stack-protector-buffer-size"="8"{{.*}} }
