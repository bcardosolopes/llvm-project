// Test X86-specific sqrt builtins

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f -target-feature +avx512fp16 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f -target-feature +avx512fp16 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512f -target-feature +avx512fp16 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

typedef float __m512 __attribute__((__vector_size__(64), __aligned__(64)));
typedef double __m512d __attribute__((__vector_size__(64), __aligned__(64)));
typedef _Float16 __m512h __attribute__((__vector_size__(64), __aligned__(64)));

// Test __builtin_ia32_sqrtph512
__m512h test_sqrtph512(__m512h a) {
  return __builtin_ia32_sqrtph512(a, 4);
}
// CIR-LABEL: cir.func {{.*}}@test_sqrtph512
// CIR: cir.sqrt {{%.*}} : !cir.vector<32 x !cir.f16>
// LLVM-LABEL: define {{.*}} @test_sqrtph512
// LLVM: call <32 x half> @llvm.sqrt.v32f16
// OGCG-LABEL: define {{.*}} @test_sqrtph512
// OGCG: call <32 x half> @llvm.sqrt.v32f16

// Test __builtin_ia32_sqrtps512
__m512 test_sqrtps512(__m512 a) {
  return __builtin_ia32_sqrtps512(a, 4);
}
// CIR-LABEL: cir.func {{.*}}@test_sqrtps512
// CIR: cir.sqrt {{%.*}} : !cir.vector<16 x !cir.float>
// LLVM-LABEL: define {{.*}} @test_sqrtps512
// LLVM: call <16 x float> @llvm.sqrt.v16f32
// OGCG-LABEL: define {{.*}} @test_sqrtps512
// OGCG: call <16 x float> @llvm.sqrt.v16f32

// Test __builtin_ia32_sqrtpd512
__m512d test_sqrtpd512(__m512d a) {
  return __builtin_ia32_sqrtpd512(a, 4);
}
// CIR-LABEL: cir.func {{.*}}@test_sqrtpd512
// CIR: cir.sqrt {{%.*}} : !cir.vector<8 x !cir.double>
// LLVM-LABEL: define {{.*}} @test_sqrtpd512
// LLVM: call <8 x double> @llvm.sqrt.v8f64
// OGCG-LABEL: define {{.*}} @test_sqrtpd512
// OGCG: call <8 x double> @llvm.sqrt.v8f64

// Test non-default rounding mode preserves AVX-512 target intrinsics.
__m512h test_sqrtph512_rounding(__m512h a) {
  return __builtin_ia32_sqrtph512(a, 11);
}
// CIR-LABEL: cir.func {{.*}}@test_sqrtph512_rounding
// CIR: cir.call_llvm_intrinsic "x86.avx512fp16.sqrt.ph.512"
// LLVM-LABEL: define {{.*}} @test_sqrtph512_rounding
// LLVM: call <32 x half> @llvm.x86.avx512fp16.sqrt.ph.512(<32 x half> {{.*}}, i32 11)
// OGCG-LABEL: define {{.*}} @test_sqrtph512_rounding
// OGCG: call <32 x half> @llvm.x86.avx512fp16.sqrt.ph.512(<32 x half> {{.*}}, i32 11)

__m512 test_sqrtps512_rounding(__m512 a) {
  return __builtin_ia32_sqrtps512(a, 11);
}
// CIR-LABEL: cir.func {{.*}}@test_sqrtps512_rounding
// CIR: cir.call_llvm_intrinsic "x86.avx512.sqrt.ps.512"
// LLVM-LABEL: define {{.*}} @test_sqrtps512_rounding
// LLVM: call <16 x float> @llvm.x86.avx512.sqrt.ps.512(<16 x float> {{.*}}, i32 11)
// OGCG-LABEL: define {{.*}} @test_sqrtps512_rounding
// OGCG: call <16 x float> @llvm.x86.avx512.sqrt.ps.512(<16 x float> {{.*}}, i32 11)

__m512d test_sqrtpd512_rounding(__m512d a) {
  return __builtin_ia32_sqrtpd512(a, 11);
}
// CIR-LABEL: cir.func {{.*}}@test_sqrtpd512_rounding
// CIR: cir.call_llvm_intrinsic "x86.avx512.sqrt.pd.512"
// LLVM-LABEL: define {{.*}} @test_sqrtpd512_rounding
// LLVM: call <8 x double> @llvm.x86.avx512.sqrt.pd.512(<8 x double> {{.*}}, i32 11)
// OGCG-LABEL: define {{.*}} @test_sqrtpd512_rounding
// OGCG: call <8 x double> @llvm.x86.avx512.sqrt.pd.512(<8 x double> {{.*}}, i32 11)
