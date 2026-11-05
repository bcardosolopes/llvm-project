// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512bitalg \
// RUN:   -target-feature +avx512vl -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512bitalg \
// RUN:   -target-feature +avx512vl -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef char __v16qi __attribute__((__vector_size__(16)));
typedef char __v32qi __attribute__((__vector_size__(32)));
typedef char __v64qi __attribute__((__vector_size__(64)));

unsigned short test_vpshufbitqmb128_mask(__v16qi a, __v16qi b,
                                         unsigned short m) {
  return __builtin_ia32_vpshufbitqmb128_mask(a, b, m);
}
// CHECK-LABEL: @test_vpshufbitqmb128_mask
// CHECK: call <16 x i1> @llvm.x86.avx512.vpshufbitqmb.128(<16 x i8> %{{.*}}, <16 x i8> %{{.*}})
// CHECK: and <16 x i1> %{{.*}}, %{{.*}}
// CHECK: bitcast <16 x i1> %{{.*}} to i16
// OGCG-LABEL: @test_vpshufbitqmb128_mask
// OGCG: call <16 x i1> @llvm.x86.avx512.vpshufbitqmb.128(<16 x i8> %{{.*}}, <16 x i8> %{{.*}})
// OGCG: and <16 x i1> %{{.*}}, %{{.*}}
// OGCG: bitcast <16 x i1> %{{.*}} to i16

unsigned short test_vpshufbitqmb128(__v16qi a, __v16qi b) {
  return __builtin_ia32_vpshufbitqmb128_mask(a, b, (unsigned short)-1);
}
// CHECK-LABEL: @test_vpshufbitqmb128
// CHECK: call <16 x i1> @llvm.x86.avx512.vpshufbitqmb.128(<16 x i8> %{{.*}}, <16 x i8> %{{.*}})
// CHECK-NOT: and <16 x i1>
// CHECK: bitcast <16 x i1> %{{.*}} to i16
// OGCG-LABEL: @test_vpshufbitqmb128
// OGCG: call <16 x i1> @llvm.x86.avx512.vpshufbitqmb.128(<16 x i8> %{{.*}}, <16 x i8> %{{.*}})
// OGCG-NOT: and <16 x i1>
// OGCG: bitcast <16 x i1> %{{.*}} to i16

unsigned int test_vpshufbitqmb256_mask(__v32qi a, __v32qi b, unsigned int m) {
  return __builtin_ia32_vpshufbitqmb256_mask(a, b, m);
}
// CHECK-LABEL: @test_vpshufbitqmb256_mask
// CHECK: call <32 x i1> @llvm.x86.avx512.vpshufbitqmb.256(<32 x i8> %{{.*}}, <32 x i8> %{{.*}})
// CHECK: and <32 x i1> %{{.*}}, %{{.*}}
// CHECK: bitcast <32 x i1> %{{.*}} to i32
// OGCG-LABEL: @test_vpshufbitqmb256_mask
// OGCG: call <32 x i1> @llvm.x86.avx512.vpshufbitqmb.256(<32 x i8> %{{.*}}, <32 x i8> %{{.*}})
// OGCG: and <32 x i1> %{{.*}}, %{{.*}}
// OGCG: bitcast <32 x i1> %{{.*}} to i32

unsigned int test_vpshufbitqmb256(__v32qi a, __v32qi b) {
  return __builtin_ia32_vpshufbitqmb256_mask(a, b, (unsigned int)-1);
}
// CHECK-LABEL: @test_vpshufbitqmb256
// CHECK: call <32 x i1> @llvm.x86.avx512.vpshufbitqmb.256(<32 x i8> %{{.*}}, <32 x i8> %{{.*}})
// CHECK-NOT: and <32 x i1>
// CHECK: bitcast <32 x i1> %{{.*}} to i32
// OGCG-LABEL: @test_vpshufbitqmb256
// OGCG: call <32 x i1> @llvm.x86.avx512.vpshufbitqmb.256(<32 x i8> %{{.*}}, <32 x i8> %{{.*}})
// OGCG-NOT: and <32 x i1>
// OGCG: bitcast <32 x i1> %{{.*}} to i32

unsigned long long test_vpshufbitqmb512_mask(__v64qi a, __v64qi b,
                                             unsigned long long m) {
  return __builtin_ia32_vpshufbitqmb512_mask(a, b, m);
}
// CHECK-LABEL: @test_vpshufbitqmb512_mask
// CHECK: call <64 x i1> @llvm.x86.avx512.vpshufbitqmb.512(<64 x i8> %{{.*}}, <64 x i8> %{{.*}})
// CHECK: and <64 x i1> %{{.*}}, %{{.*}}
// CHECK: bitcast <64 x i1> %{{.*}} to i64
// OGCG-LABEL: @test_vpshufbitqmb512_mask
// OGCG: call <64 x i1> @llvm.x86.avx512.vpshufbitqmb.512(<64 x i8> %{{.*}}, <64 x i8> %{{.*}})
// OGCG: and <64 x i1> %{{.*}}, %{{.*}}
// OGCG: bitcast <64 x i1> %{{.*}} to i64

unsigned long long test_vpshufbitqmb512(__v64qi a, __v64qi b) {
  return __builtin_ia32_vpshufbitqmb512_mask(a, b, (unsigned long long)-1);
}
// CHECK-LABEL: @test_vpshufbitqmb512
// CHECK: call <64 x i1> @llvm.x86.avx512.vpshufbitqmb.512(<64 x i8> %{{.*}}, <64 x i8> %{{.*}})
// CHECK-NOT: and <64 x i1>
// CHECK: bitcast <64 x i1> %{{.*}} to i64
// OGCG-LABEL: @test_vpshufbitqmb512
// OGCG: call <64 x i1> @llvm.x86.avx512.vpshufbitqmb.512(<64 x i8> %{{.*}}, <64 x i8> %{{.*}})
// OGCG-NOT: and <64 x i1>
// OGCG: bitcast <64 x i1> %{{.*}} to i64
