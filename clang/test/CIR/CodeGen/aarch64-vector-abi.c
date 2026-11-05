// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: vector types pass through AArch64 ABI correctly.

typedef float v4f __attribute__((vector_size(16)));
typedef int v4i __attribute__((vector_size(16)));

v4f add_vectors(v4f a, v4f b) { return a + b; }

// LLVM: define {{.*}} <4 x float> @add_vectors(<4 x float> noundef %{{.*}}, <4 x float> noundef %{{.*}})
// OGCG: define {{.*}} <4 x float> @add_vectors(<4 x float> noundef %{{.*}}, <4 x float> noundef %{{.*}})
