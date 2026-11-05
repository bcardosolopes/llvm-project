// RUN: %clang_cc1 -triple aarch64-linux-gnu -fclangir -emit-llvm -O1 %s -o - \
// RUN:   | FileCheck %s --check-prefix=AARCH64
// RUN: %clang_cc1 -triple aarch64-linux-gnu -emit-llvm -O1 %s -o - \
// RUN:   | FileCheck %s --check-prefix=AARCH64
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm -O1 %s -o - \
// RUN:   | FileCheck %s --check-prefix=X86
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm -O1 %s -o - \
// RUN:   | FileCheck %s --check-prefix=X86

// Verify that __builtin_clz/__builtin_ctz use the target's
// isCLZForZeroUndef() to set the is_zero_poison flag.
// AArch64: false (CLZ/CTZ instructions are well-defined for zero)
// x86_64: true (BSR/BSF are undefined for zero)

int test_clz(unsigned x) {
    return __builtin_clz(x);
}

int test_ctz(unsigned x) {
    return __builtin_ctz(x);
}

// AARCH64: call {{.*}} i32 @llvm.ctlz.i32(i32 {{.*}}, i1 false)
// AARCH64: call {{.*}} i32 @llvm.cttz.i32(i32 {{.*}}, i1 false)

// X86: call {{.*}} i32 @llvm.ctlz.i32(i32 {{.*}}, i1 true)
// X86: call {{.*}} i32 @llvm.cttz.i32(i32 {{.*}}, i1 true)
