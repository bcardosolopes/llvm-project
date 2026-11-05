// RUN: %clang --target=x86_64-unknown-linux-gnu -S -emit-llvm -O0 -fclangir %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang --target=x86_64-unknown-linux-gnu -S -emit-llvm -O0 -fno-clangir %s -o - | FileCheck %s --check-prefix=OGCG
//
// Regression: by-value bitfield aggregate temporary should use align 4 in the
// coerced alloca/store path, matching OGCG.

struct S {
  unsigned a : 3;
};

int f(struct S s) { return s.a; }

// LLVM-LABEL: define {{.*}} @f(i32
// LLVM: alloca %struct.S, i64 1, align 4
// LLVM: store i32 {{.*}}, ptr {{.*}}, align 4

// OGCG-LABEL: define {{.*}} @f(i32
// OGCG: alloca %struct.S, align 4
// OGCG: store i32 {{.*}}, ptr {{.*}}, align 4
