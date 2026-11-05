// RUN: %clang_cc1 -triple armv7-none-linux-androideabi -mfloat-abi softfp -fclangir -emit-llvm %s -o %t.arm.ll
// RUN: FileCheck --input-file=%t.arm.ll %s --check-prefix=ARM
// RUN: %clang_cc1 -triple aarch64-none-linux-android -fclangir -emit-llvm %s -o %t.a64.ll
// RUN: FileCheck --input-file=%t.a64.ll %s --check-prefix=AARCH64

// The llvm.memset emitted for aggregate zero-initialization must use the
// target's size_t width for the length: i32 on 32-bit ARM, i64 on AArch64.
// Previously the length was hardcoded to i64, producing an i64-length memset on
// 32-bit targets (divergent from classic codegen).

struct Big { int a[20]; };

// ARM: call void @llvm.memset.p0.i32(ptr {{.*}}, i8 0, i32 80, i1 false)
// AARCH64: call void @llvm.memset.p0.i64(ptr {{.*}}, i8 0, i64 80, i1 false)
void zero(struct Big *d) {
  struct Big z = {0};
  *d = z;
}
