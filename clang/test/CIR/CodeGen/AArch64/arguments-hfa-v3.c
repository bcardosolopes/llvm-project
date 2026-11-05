// RUN: %clang_cc1 -triple arm64-apple-ios -target-feature +neon -target-abi darwinpcs -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple arm64-apple-ios -target-feature +neon -target-abi darwinpcs -emit-llvm -o - %s | FileCheck --check-prefix=OGCG %s

typedef __attribute__((__ext_vector_type__(16))) signed char int8x16_t;
typedef __attribute__((__ext_vector_type__(3))) float float32x3_t;

// CHECK: %struct.HFAv3 = type { [4 x <3 x float>] }
// OGCG: %struct.HFAv3 = type { [4 x <3 x float>] }
typedef struct { float32x3_t arr[4]; } HFAv3;

// CHECK: %struct.MixedHFAv3 = type { [3 x <3 x float>], <16 x i8> }
// OGCG: %struct.MixedHFAv3 = type { [3 x <3 x float>], <16 x i8> }
typedef struct { float32x3_t arr[3]; int8x16_t b; } MixedHFAv3;

// HFAs with v3 vector members should be passed using widened [4 x <4 x float>]
// coercion type without alignstack, matching OGCG's DarwinPCS behavior.

// CHECK: define{{.*}} %struct.HFAv3 @test([4 x <4 x float>] %{{.*}}, [4 x <4 x float>] %{{.*}}, [4 x <4 x float>] %{{.*}})
// OGCG: define{{.*}} %struct.HFAv3 @test([4 x <4 x float>] %{{.*}}, [4 x <4 x float>] %{{.*}}, [4 x <4 x float>] %{{.*}})
HFAv3 test(HFAv3 a0, HFAv3 a1, HFAv3 a2) {
  return a2;
}

// CHECK: define{{.*}} %struct.MixedHFAv3 @test_mixed([4 x <4 x float>] %{{.*}}, [4 x <4 x float>] %{{.*}}, [4 x <4 x float>] %{{.*}})
// OGCG: define{{.*}} %struct.MixedHFAv3 @test_mixed([4 x <4 x float>] %{{.*}}, [4 x <4 x float>] %{{.*}}, [4 x <4 x float>] %{{.*}})
MixedHFAv3 test_mixed(MixedHFAv3 a0, MixedHFAv3 a1, MixedHFAv3 a2) {
  return a2;
}
