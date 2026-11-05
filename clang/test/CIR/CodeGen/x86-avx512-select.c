// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512bw \
// RUN:   -target-feature +avx512vl -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512bw \
// RUN:   -target-feature +avx512vl -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef char __v16qi __attribute__((__vector_size__(16)));
typedef int __v4si __attribute__((__vector_size__(16)));
typedef float __v4sf __attribute__((__vector_size__(16)));
typedef double __v2df __attribute__((__vector_size__(16)));

// Vector byte select
__v16qi test_selectb_128(unsigned short m, __v16qi a, __v16qi b) {
  return __builtin_ia32_selectb_128(m, a, b);
}
// CHECK-LABEL: @test_selectb_128
// CHECK: select <16 x i1>
// OGCG-LABEL: @test_selectb_128
// OGCG: select <16 x i1>

// Vector dword select
__v4si test_selectd_128(unsigned char m, __v4si a, __v4si b) {
  return __builtin_ia32_selectd_128(m, a, b);
}
// CHECK-LABEL: @test_selectd_128
// CHECK: select <4 x i1>
// OGCG-LABEL: @test_selectd_128
// OGCG: select <4 x i1>

// Vector float select
__v4sf test_selectps_128(unsigned char m, __v4sf a, __v4sf b) {
  return __builtin_ia32_selectps_128(m, a, b);
}
// CHECK-LABEL: @test_selectps_128
// CHECK: select <4 x i1>
// OGCG-LABEL: @test_selectps_128
// OGCG: select <4 x i1>

// Vector double select
__v2df test_selectpd_128(unsigned char m, __v2df a, __v2df b) {
  return __builtin_ia32_selectpd_128(m, a, b);
}
// CHECK-LABEL: @test_selectpd_128
// CHECK: select <2 x i1>
// OGCG-LABEL: @test_selectpd_128
// OGCG: select <2 x i1>

// All-ones mask should optimize away the select
__v4si test_selectd_128_allones(__v4si a, __v4si b) {
  return __builtin_ia32_selectd_128(-1, a, b);
}
// CHECK-LABEL: @test_selectd_128_allones
// CHECK-NOT: select
// CHECK: ret
// OGCG-LABEL: @test_selectd_128_allones
// OGCG-NOT: select
// OGCG: ret

// Scalar float select
__v4sf test_selectss_128(unsigned char m, __v4sf a, __v4sf b) {
  return __builtin_ia32_selectss_128(m, a, b);
}
// CHECK-LABEL: @test_selectss_128
// CHECK: extractelement <4 x float> %{{.*}}, i{{32|64}} 0
// CHECK: extractelement <4 x float> %{{.*}}, i{{32|64}} 0
// CHECK: select i1
// CHECK: insertelement <4 x float>
// OGCG-LABEL: @test_selectss_128
// OGCG: extractelement <4 x float> %{{.*}}, i{{32|64}} 0
// OGCG: extractelement <4 x float> %{{.*}}, i{{32|64}} 0
// OGCG: select i1
// OGCG: insertelement <4 x float>

// Scalar double select
__v2df test_selectsd_128(unsigned char m, __v2df a, __v2df b) {
  return __builtin_ia32_selectsd_128(m, a, b);
}
// CHECK-LABEL: @test_selectsd_128
// CHECK: extractelement <2 x double> %{{.*}}, i{{32|64}} 0
// CHECK: extractelement <2 x double> %{{.*}}, i{{32|64}} 0
// CHECK: select i1
// CHECK: insertelement <2 x double>
// OGCG-LABEL: @test_selectsd_128
// OGCG: extractelement <2 x double> %{{.*}}, i{{32|64}} 0
// OGCG: extractelement <2 x double> %{{.*}}, i{{32|64}} 0
// OGCG: select i1
// OGCG: insertelement <2 x double>
