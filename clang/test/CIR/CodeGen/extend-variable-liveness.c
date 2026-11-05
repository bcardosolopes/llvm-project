// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi -fextend-variable-liveness=all %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fextend-variable-liveness=all %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll

// Test that -fextend-variable-liveness=all emits llvm.fake.use calls for
// both local variables and parameters at scope exit. The fake use is emitted
// through the LLVM-intrinsic call path so it lowers to the recognized
// `llvm.fake.use` intrinsic (see fake-use-intrinsic.cpp).

int test_int(int x) {
  int y = x + 1;
  return y;
}

// CIR-LABEL: cir.func no_inline optnone dso_local @test_int(
// CIR: %[[X_ADDR:.+]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["x", init]
// CIR: %[[Y_ADDR:.+]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["y", init]
// CIR: %[[RET:.+]] = cir.load align(4) %[[Y_ADDR]]
// CIR: %[[Y_FAKE:.+]] = cir.load align(4) %[[Y_ADDR]]
// CIR-NEXT: cir.call_llvm_intrinsic "fake.use" %[[Y_FAKE]] : (!s32i) -> !void {no_tail, nothrow}
// CIR: %[[X_FAKE:.+]] = cir.load align(4) %[[X_ADDR]]
// CIR-NEXT: cir.call_llvm_intrinsic "fake.use" %[[X_FAKE]] : (!s32i) -> !void {no_tail, nothrow}
// CIR: cir.return %[[RET]]
// LLVM-LABEL: define dso_local i32 @test_int(i32 noundef
// LLVM: %[[X_ADDR:[^ ]+]] = alloca i32, i64 1, align 4
// LLVM: %[[Y_ADDR:[^ ]+]] = alloca i32, i64 1, align 4
// LLVM: %[[RET:[^ ]+]] = load i32, ptr %[[Y_ADDR]], align 4
// LLVM: %[[Y_FAKE:[^ ]+]] = load i32, ptr %[[Y_ADDR]], align 4
// LLVM-NEXT: call void (...) @llvm.fake.use(i32 %[[Y_FAKE]])
// LLVM: %[[X_FAKE:[^ ]+]] = load i32, ptr %[[X_ADDR]], align 4
// LLVM-NEXT: call void (...) @llvm.fake.use(i32 %[[X_FAKE]])
// LLVM: ret i32 %[[RET]]

float test_float(float f) {
  float g = f * 2.0f;
  return g;
}

// CIR-LABEL: cir.func no_inline optnone dso_local @test_float(
// CIR: %[[F_ADDR:.+]] = cir.alloca !cir.float, !cir.ptr<!cir.float>, ["f", init]
// CIR: %[[G_ADDR:.+]] = cir.alloca !cir.float, !cir.ptr<!cir.float>, ["g", init]
// CIR: %[[RET:.+]] = cir.load align(4) %[[G_ADDR]]
// CIR: %[[G_FAKE:.+]] = cir.load align(4) %[[G_ADDR]]
// CIR-NEXT: cir.call_llvm_intrinsic "fake.use" %[[G_FAKE]] : (!cir.float) -> !void {no_tail, nothrow}
// CIR: %[[F_FAKE:.+]] = cir.load align(4) %[[F_ADDR]]
// CIR-NEXT: cir.call_llvm_intrinsic "fake.use" %[[F_FAKE]] : (!cir.float) -> !void {no_tail, nothrow}
// CIR: cir.return %[[RET]]
// LLVM-LABEL: define dso_local float @test_float(float noundef
// LLVM: %[[F_ADDR:[^ ]+]] = alloca float, i64 1, align 4
// LLVM: %[[G_ADDR:[^ ]+]] = alloca float, i64 1, align 4
// LLVM: %[[RET:[^ ]+]] = load float, ptr %[[G_ADDR]], align 4
// LLVM: %[[G_FAKE:[^ ]+]] = load float, ptr %[[G_ADDR]], align 4
// LLVM-NEXT: call void (...) @llvm.fake.use(float %[[G_FAKE]])
// LLVM: %[[F_FAKE:[^ ]+]] = load float, ptr %[[F_ADDR]], align 4
// LLVM-NEXT: call void (...) @llvm.fake.use(float %[[F_FAKE]])
// LLVM: ret float %[[RET]]
