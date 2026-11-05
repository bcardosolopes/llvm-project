// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +crc \
// RUN:   -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +crc \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;

uint32_t test_crc32b(uint32_t a, uint8_t b) {
  return __builtin_arm_crc32b(a, b);
}
// CIR-LABEL: @test_crc32b
// CIR: cir.call_llvm_intrinsic "aarch64.crc32b"
// LLVM-LABEL: @test_crc32b
// LLVM: call i32 @llvm.aarch64.crc32b(i32 %{{.*}}, i32 %{{.*}})

uint32_t test_crc32h(uint32_t a, uint16_t b) {
  return __builtin_arm_crc32h(a, b);
}
// CIR-LABEL: @test_crc32h
// CIR: cir.call_llvm_intrinsic "aarch64.crc32h"
// LLVM-LABEL: @test_crc32h
// LLVM: call i32 @llvm.aarch64.crc32h(i32 %{{.*}}, i32 %{{.*}})

uint32_t test_crc32w(uint32_t a, uint32_t b) {
  return __builtin_arm_crc32w(a, b);
}
// CIR-LABEL: @test_crc32w
// CIR: cir.call_llvm_intrinsic "aarch64.crc32w"
// LLVM-LABEL: @test_crc32w
// LLVM: call i32 @llvm.aarch64.crc32w(i32 %{{.*}}, i32 %{{.*}})

uint32_t test_crc32d(uint32_t a, uint64_t b) {
  return __builtin_arm_crc32d(a, b);
}
// CIR-LABEL: @test_crc32d
// CIR: cir.call_llvm_intrinsic "aarch64.crc32x"
// LLVM-LABEL: @test_crc32d
// LLVM: call i32 @llvm.aarch64.crc32x(i32 %{{.*}}, i64 %{{.*}})

uint32_t test_crc32cb(uint32_t a, uint8_t b) {
  return __builtin_arm_crc32cb(a, b);
}
// CIR-LABEL: @test_crc32cb
// CIR: cir.call_llvm_intrinsic "aarch64.crc32cb"
// LLVM-LABEL: @test_crc32cb
// LLVM: call i32 @llvm.aarch64.crc32cb(i32 %{{.*}}, i32 %{{.*}})

uint32_t test_crc32ch(uint32_t a, uint16_t b) {
  return __builtin_arm_crc32ch(a, b);
}
// CIR-LABEL: @test_crc32ch
// CIR: cir.call_llvm_intrinsic "aarch64.crc32ch"
// LLVM-LABEL: @test_crc32ch
// LLVM: call i32 @llvm.aarch64.crc32ch(i32 %{{.*}}, i32 %{{.*}})

uint32_t test_crc32cw(uint32_t a, uint32_t b) {
  return __builtin_arm_crc32cw(a, b);
}
// CIR-LABEL: @test_crc32cw
// CIR: cir.call_llvm_intrinsic "aarch64.crc32cw"
// LLVM-LABEL: @test_crc32cw
// LLVM: call i32 @llvm.aarch64.crc32cw(i32 %{{.*}}, i32 %{{.*}})

uint32_t test_crc32cd(uint32_t a, uint64_t b) {
  return __builtin_arm_crc32cd(a, b);
}
// CIR-LABEL: @test_crc32cd
// CIR: cir.call_llvm_intrinsic "aarch64.crc32cx"
// LLVM-LABEL: @test_crc32cd
// LLVM: call i32 @llvm.aarch64.crc32cx(i32 %{{.*}}, i64 %{{.*}})
