// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -verify %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -verify %s

// clangir must emit the same "AVX vector argument ... changes the ABI" hard
// error as classic CodeGen when a >128-bit vector crosses a target-feature
// boundary by value. Without this diagnostic clangir silently miscompiled the
// call (baseline caller passes the vector as xmm-pairs, the target("avx2")
// callee reads it from ymm -> ABI mismatch -> wrong result).

typedef int v8si __attribute__((vector_size(32)));

__attribute__((noinline, target("avx2"))) v8si add8(v8si a, v8si b) {
  return a + b;
}

int use(void) {
  v8si x = {1, 2, 3, 4, 5, 6, 7, 8};
  v8si y = {10, 20, 30, 40, 50, 60, 70, 80};
  // expected-error@+1 {{AVX vector argument of type 'v8si' (vector of 8 'int' values) without 'avx' enabled changes the ABI}}
  v8si r = add8(x, y);
  return r[0];
}
