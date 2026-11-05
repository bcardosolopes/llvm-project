// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fclangir -emit-llvm -target-feature +avx -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM %s < %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm -target-feature +avx -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s < %t.ogcg.ll

// Verify that 256-bit AVX types are passed in YMM registers (<4 x double>)
// rather than through memory (sret/byval) when -target-feature +avx is used.

typedef double __m256d __attribute__((__vector_size__(32)));

class PR22753 {
public:
  __m256d data;
};

// LLVM: define dso_local <4 x double> @_Z4test7PR22753(<4 x double>
// OGCG: define dso_local <4 x double> @_Z4test7PR22753(<4 x double>
PR22753 test(PR22753 x) {
  return x;
}

typedef double __m128d __attribute__((__vector_size__(16)));

union U128 {
  __m128d v;
};

// 128-bit types should also use register passing.
// LLVM: define dso_local <2 x double> @_Z7test1284U128(<2 x double>
// OGCG: define dso_local <2 x double> @_Z7test1284U128(<2 x double>
U128 test128(U128 x) {
  return x;
}

union U256 {
  __m256d v;
};

// 256-bit union types should use register passing with +avx.
// LLVM: define dso_local <4 x double> @_Z7test2564U256(<4 x double>
// OGCG: define dso_local <4 x double> @_Z7test2564U256(<4 x double>
U256 test256(U256 x) {
  return x;
}
