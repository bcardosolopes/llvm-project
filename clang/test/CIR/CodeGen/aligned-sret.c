// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: sret/byval alignment respects __aligned__ attribute.

typedef __attribute__((__ext_vector_type__(4),__aligned__(16))) double simd_double4;
typedef struct { simd_double4 columns[4]; } simd_double4x4;

simd_double4x4 ident(simd_double4x4 x) { return x; }

// LLVM: define {{.*}} @ident(ptr {{.*}} sret(%struct.simd_double4x4) align 16 %{{.*}}, ptr {{.*}} byval(%struct.simd_double4x4) align 16 %{{.*}})
// OGCG: define {{.*}} @ident(ptr {{.*}} sret(%struct.simd_double4x4) align 16 %{{.*}}, ptr {{.*}} byval(%struct.simd_double4x4) align 16 %{{.*}})
