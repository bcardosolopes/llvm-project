// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512vbmi \
// RUN:   -target-feature +avx512vl -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx512vbmi \
// RUN:   -target-feature +avx512vl -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef char __v16qi __attribute__((__vector_size__(16)));
typedef char __v32qi __attribute__((__vector_size__(32)));
typedef char __v64qi __attribute__((__vector_size__(64)));

__v16qi test_vpmultishiftqb128(__v16qi x, __v16qi y) {
  return __builtin_ia32_vpmultishiftqb128(x, y);
}
// CHECK-LABEL: @test_vpmultishiftqb128
// CHECK: call <16 x i8> @llvm.x86.avx512.pmultishift.qb.128(<16 x i8> %{{.*}}, <16 x i8> %{{.*}})
// OGCG-LABEL: @test_vpmultishiftqb128
// OGCG: call <16 x i8> @llvm.x86.avx512.pmultishift.qb.128(<16 x i8> %{{.*}}, <16 x i8> %{{.*}})

__v16qi test_vpmultishiftqb128_mask(__v16qi w, unsigned short m, __v16qi x,
                                    __v16qi y) {
  return __builtin_ia32_selectb_128(m, __builtin_ia32_vpmultishiftqb128(x, y),
                                    w);
}
// CHECK-LABEL: @test_vpmultishiftqb128_mask
// CHECK: call <16 x i8> @llvm.x86.avx512.pmultishift.qb.128(<16 x i8> %{{.*}}, <16 x i8> %{{.*}})
// CHECK: select <16 x i1> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}}
// OGCG-LABEL: @test_vpmultishiftqb128_mask
// OGCG: call <16 x i8> @llvm.x86.avx512.pmultishift.qb.128(<16 x i8> %{{.*}}, <16 x i8> %{{.*}})
// OGCG: select <16 x i1> %{{.*}}, <16 x i8> %{{.*}}, <16 x i8> %{{.*}}

__v32qi test_vpmultishiftqb256(__v32qi x, __v32qi y) {
  return __builtin_ia32_vpmultishiftqb256(x, y);
}
// CHECK-LABEL: @test_vpmultishiftqb256
// CHECK: call <32 x i8> @llvm.x86.avx512.pmultishift.qb.256(<32 x i8> %{{.*}}, <32 x i8> %{{.*}})
// OGCG-LABEL: @test_vpmultishiftqb256
// OGCG: call <32 x i8> @llvm.x86.avx512.pmultishift.qb.256(<32 x i8> %{{.*}}, <32 x i8> %{{.*}})

__v32qi test_vpmultishiftqb256_mask(__v32qi w, unsigned int m, __v32qi x,
                                    __v32qi y) {
  return __builtin_ia32_selectb_256(m, __builtin_ia32_vpmultishiftqb256(x, y),
                                    w);
}
// CHECK-LABEL: @test_vpmultishiftqb256_mask
// CHECK: call <32 x i8> @llvm.x86.avx512.pmultishift.qb.256(<32 x i8> %{{.*}}, <32 x i8> %{{.*}})
// CHECK: select <32 x i1> %{{.*}}, <32 x i8> %{{.*}}, <32 x i8> %{{.*}}
// OGCG-LABEL: @test_vpmultishiftqb256_mask
// OGCG: call <32 x i8> @llvm.x86.avx512.pmultishift.qb.256(<32 x i8> %{{.*}}, <32 x i8> %{{.*}})
// OGCG: select <32 x i1> %{{.*}}, <32 x i8> %{{.*}}, <32 x i8> %{{.*}}

__v64qi test_vpmultishiftqb512(__v64qi x, __v64qi y) {
  return __builtin_ia32_vpmultishiftqb512(x, y);
}
// CHECK-LABEL: @test_vpmultishiftqb512
// CHECK: call <64 x i8> @llvm.x86.avx512.pmultishift.qb.512(<64 x i8> %{{.*}}, <64 x i8> %{{.*}})
// OGCG-LABEL: @test_vpmultishiftqb512
// OGCG: call <64 x i8> @llvm.x86.avx512.pmultishift.qb.512(<64 x i8> %{{.*}}, <64 x i8> %{{.*}})

__v64qi test_vpmultishiftqb512_mask(__v64qi w, unsigned long long m, __v64qi x,
                                    __v64qi y) {
  return __builtin_ia32_selectb_512(m, __builtin_ia32_vpmultishiftqb512(x, y),
                                    w);
}
// CHECK-LABEL: @test_vpmultishiftqb512_mask
// CHECK: call <64 x i8> @llvm.x86.avx512.pmultishift.qb.512(<64 x i8> %{{.*}}, <64 x i8> %{{.*}})
// CHECK: select <64 x i1> %{{.*}}, <64 x i8> %{{.*}}, <64 x i8> %{{.*}}
// OGCG-LABEL: @test_vpmultishiftqb512_mask
// OGCG: call <64 x i8> @llvm.x86.avx512.pmultishift.qb.512(<64 x i8> %{{.*}}, <64 x i8> %{{.*}})
// OGCG: select <64 x i1> %{{.*}}, <64 x i8> %{{.*}}, <64 x i8> %{{.*}}
