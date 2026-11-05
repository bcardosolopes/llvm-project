// RUN: %clang_cc1 -x c -ffreestanding -triple x86_64-unknown-linux -fclangir -emit-cir -o %t.cir %s -target-feature +sse4.2
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -x c -ffreestanding -triple x86_64-unknown-linux -fclangir -emit-llvm -o %t.ll %s -target-feature +sse4.2
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -x c -ffreestanding -triple x86_64-unknown-linux -emit-llvm -o %t.ogcg.ll %s -target-feature +sse4.2
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test: CRC32 intrinsics return signed i32 from the LLVM intrinsic
// but the C-level return type is unsigned int. CIR must cast the result.

unsigned int test_crc32qi(unsigned int c, unsigned char d) {
  // CIR-LABEL: @test_crc32qi
  // CIR: %{{.*}} = cir.call_llvm_intrinsic "x86.sse42.crc32.32.8" %{{.*}}, %{{.*}} : (!u32i, !u8i) -> !s32i
  // CIR: %{{.*}} = cir.cast integral %{{.*}} : !s32i -> !u32i
  // LLVM-LABEL: @test_crc32qi
  // LLVM: call i32 @llvm.x86.sse42.crc32.32.8(i32 %{{.*}}, i8 %{{.*}})
  // OGCG-LABEL: @test_crc32qi
  // OGCG: call i32 @llvm.x86.sse42.crc32.32.8(i32 %{{.*}}, i8 %{{.*}})
  return __builtin_ia32_crc32qi(c, d);
}

unsigned int test_crc32hi(unsigned int c, unsigned short d) {
  // CIR-LABEL: @test_crc32hi
  // CIR: %{{.*}} = cir.call_llvm_intrinsic "x86.sse42.crc32.32.16" %{{.*}}, %{{.*}} : (!u32i, !u16i) -> !s32i
  // CIR: %{{.*}} = cir.cast integral %{{.*}} : !s32i -> !u32i
  // LLVM-LABEL: @test_crc32hi
  // LLVM: call i32 @llvm.x86.sse42.crc32.32.16(i32 %{{.*}}, i16 %{{.*}})
  // OGCG-LABEL: @test_crc32hi
  // OGCG: call i32 @llvm.x86.sse42.crc32.32.16(i32 %{{.*}}, i16 %{{.*}})
  return __builtin_ia32_crc32hi(c, d);
}

unsigned int test_crc32si(unsigned int c, unsigned int d) {
  // CIR-LABEL: @test_crc32si
  // CIR: %{{.*}} = cir.call_llvm_intrinsic "x86.sse42.crc32.32.32" %{{.*}}, %{{.*}} : (!u32i, !u32i) -> !s32i
  // CIR: %{{.*}} = cir.cast integral %{{.*}} : !s32i -> !u32i
  // LLVM-LABEL: @test_crc32si
  // LLVM: call i32 @llvm.x86.sse42.crc32.32.32(i32 %{{.*}}, i32 %{{.*}})
  // OGCG-LABEL: @test_crc32si
  // OGCG: call i32 @llvm.x86.sse42.crc32.32.32(i32 %{{.*}}, i32 %{{.*}})
  return __builtin_ia32_crc32si(c, d);
}
