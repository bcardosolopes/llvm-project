// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -mllvm -expand-hfa-return-type \
// RUN:   -emit-llvm -o - %s | FileCheck --check-prefix=IR %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -mllvm -expand-hfa-return-type \
// RUN:   -mrelocation-model static -fno-verbose-asm -S -O -o - %s | FileCheck --check-prefix=ASM %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -mllvm -expand-hfa-return-type=0 \
// RUN:   -mrelocation-model static -fno-verbose-asm -S -O -o - %s | FileCheck --check-prefix=ASM %s

// IR: %struct.HFAFloat = type { float, float }
typedef struct { float f; float g; } HFAFloat;

// IR: %struct.HFADouble = type { [3 x double] }
typedef struct { double d[3]; } HFADouble;

// From aarch64-arguments-hfa-v3.c
typedef __attribute__((__ext_vector_type__(16))) signed char int8x16_t;
typedef __attribute__((__ext_vector_type__(3))) float float32x3_t;

// IR: %struct.HFAv3 = type { [4 x <3 x float>] }
typedef struct { float32x3_t arr[4]; } HFAv3;

// IR: %struct.MixedHFAv3 = type { [3 x <3 x float>], <16 x i8> }
typedef struct { float32x3_t arr[3]; int8x16_t b; } MixedHFAv3;

// IR: define{{.*}} [2 x float] @test_hfa_float()
// ASM-LABEL: test_hfa_float:
// ASM-NEXT:          adrp    x8, hfaFloat
// ASM-NEXT:          add     x8, x8, :lo12:hfaFloat
// ASM-NEXT:          ldp     s0, s1, [x8]
// ASM-NEXT:          ret
HFAFloat hfaFloat;
HFAFloat test_hfa_float() {
  return hfaFloat;
}

// IR: define{{.*}} [3 x double] @test_hfa_double([2 x float] {{.*}}, [3 x double] {{.*}})
// ASM-LABEL: test_hfa_double:
// ASM-NEXT:          fmov    d1, d3
// ASM-NEXT:          fmov    d0, d2
// ASM-NEXT:          fmov    d2, d4
// ASM-NEXT:          ret
HFADouble test_hfa_double(HFAFloat f, HFADouble d) {
  return d;
}

// IR: define{{.*}} [4 x <4 x float>] @test0([4 x <4 x float>] {{.*}}, [4 x <4 x float>] {{.*}}, [4 x <4 x float>] {{.*}})
// ASM-LABEL: test0:
// ASM-NEXT:          ret
HFAv3 test0(HFAv3 a0, HFAv3 a1, HFAv3 a2) {
  return a0;
}

// IR: define{{.*}} [4 x <4 x float>] @test1([4 x <4 x float>] {{.*}}, [4 x <4 x float>] {{.*}}, [4 x <4 x float>] {{.*}})
// ASM-LABEL: test1:
// ASM-NEXT:          mov     v3.16b, v7.16b
// ASM-NEXT:          mov     v2.16b, v6.16b
// ASM-NEXT:          mov     v0.16b, v4.16b
// ASM-NEXT:          mov     v1.16b, v5.16b
// ASM-NEXT:          ret
HFAv3 test1(HFAv3 a0, HFAv3 a1, HFAv3 a2) {
  return a1;
}

// IR: define{{.*}} [4 x <4 x float>] @test2([4 x <4 x float>] {{.*}}, [4 x <4 x float>] {{.*}}, [4 x <4 x float>] {{.*}})
// ASM-LABEL: test2:
// ASM:               ldp     q0, q1, [sp]
// ASM-NEXT:          ldp     q2, q3, [sp, #32]
// ASM-NEXT:          ret
HFAv3 test2(HFAv3 a0, HFAv3 a1, HFAv3 a2) {
  return a2;
}

// IR: define{{.*}} [4 x <4 x float>] @test_mixed([4 x <4 x float>] {{.*}}, [4 x <4 x float>] {{.*}}, [4 x <4 x float>] {{.*}})
// ASM-LABEL: test_mixed:
// ASM:               ldp     q0, q1, [sp]
// ASM-NEXT:          ldp     q2, q3, [sp, #32]
// ASM-NEXT:          ret
MixedHFAv3 test_mixed(MixedHFAv3 a0, MixedHFAv3 a1, MixedHFAv3 a2) {
  return a2;
}
