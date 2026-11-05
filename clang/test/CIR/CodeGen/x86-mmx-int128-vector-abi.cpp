// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.linux.ll
// RUN: FileCheck --input-file=%t.linux.ll --check-prefix=LINUX %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin -fclangir -emit-llvm %s -o %t.darwin.ll
// RUN: FileCheck --input-file=%t.darwin.ll --check-prefix=DARWIN %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx -fclangir -emit-llvm %s -o %t.linux.avx.ll
// RUN: FileCheck --input-file=%t.linux.avx.ll --check-prefix=LINUX-AVX %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclang-abi-compat=9 -fclangir -emit-llvm %s -o %t.linux.compat9.ll
// RUN: FileCheck --input-file=%t.linux.compat9.ll --check-prefix=LINUX-COMPAT9 %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclang-abi-compat=9 -target-feature +avx -fclangir -emit-llvm %s -o %t.linux.compat9.avx.ll
// RUN: FileCheck --input-file=%t.linux.compat9.avx.ll --check-prefix=LINUX-COMPAT9-AVX %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin -target-feature +avx -fclangir -emit-llvm %s -o %t.darwin.avx.ll
// RUN: FileCheck --input-file=%t.darwin.avx.ll --check-prefix=DARWIN-AVX %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.linux.ll
// RUN: FileCheck --input-file=%t.ogcg.linux.ll --check-prefix=LINUX %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin -emit-llvm %s -o %t.ogcg.darwin.ll
// RUN: FileCheck --input-file=%t.ogcg.darwin.ll --check-prefix=DARWIN %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +avx -emit-llvm %s -o %t.ogcg.linux.avx.ll
// RUN: FileCheck --input-file=%t.ogcg.linux.avx.ll --check-prefix=LINUX-AVX %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclang-abi-compat=9 -emit-llvm %s -o %t.ogcg.linux.compat9.ll
// RUN: FileCheck --input-file=%t.ogcg.linux.compat9.ll --check-prefix=LINUX-COMPAT9 %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclang-abi-compat=9 -target-feature +avx -emit-llvm %s -o %t.ogcg.linux.compat9.avx.ll
// RUN: FileCheck --input-file=%t.ogcg.linux.compat9.avx.ll --check-prefix=LINUX-COMPAT9-AVX %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin -target-feature +avx -emit-llvm %s -o %t.ogcg.darwin.avx.ll
// RUN: FileCheck --input-file=%t.ogcg.darwin.avx.ll --check-prefix=DARWIN-AVX %s

typedef long long v1ll __attribute__((vector_size(8)));

// Linux: <1 x long long> is passed as SSE-coerced double.
// LINUX: define{{.*}} double @{{.*}}pass_mmx{{.*}}(double noundef
// Darwin: keep legacy integer passing for platform ABI compatibility.
// DARWIN: define{{.*}} i64 @{{.*}}pass_mmx{{.*}}(i64 noundef
v1ll pass_mmx(v1ll x) { return x; }

typedef unsigned __int128 u128;
typedef u128 v1u128 __attribute__((vector_size(16)));

// On Linux/NetBSD with the default ABI, 128-bit vectors of __int128 are
// coerced to a v2i64 register type rather than being passed in their native
// one-lane i128 vector shape. Darwin keeps the legacy native-vector form.
// LINUX: define{{.*}} <2 x i64> @{{.*}}pass_i128_vec128{{.*}}(<2 x i64> noundef
// LINUX-COMPAT9: define{{.*}} <1 x i128> @{{.*}}pass_i128_vec128{{.*}}(<1 x i128> noundef
// DARWIN: define{{.*}} <1 x i128> @{{.*}}pass_i128_vec128{{.*}}(<1 x i128> noundef
v1u128 pass_i128_vec128(v1u128 x) { return x; }

typedef u128 v2u128 __attribute__((vector_size(32)));

// Without AVX, both targets pass 256-bit vectors in memory.
// LINUX: define{{.*}} <2 x i128> @{{.*}}pass_i128_vec{{.*}}(ptr noundef byval(<2 x i128>) align {{(16|32)}}
// DARWIN: define{{.*}} <2 x i128> @{{.*}}pass_i128_vec{{.*}}(ptr noundef byval(<2 x i128>) align {{(16|32)}}
// With AVX enabled, Linux/NetBSD keep __int128 vectors in memory but Darwin
// passes them directly.
// LINUX-AVX: define{{.*}} <2 x i128> @{{.*}}pass_i128_vec{{.*}}(ptr noundef byval(<2 x i128>) align {{(16|32)}}
// LINUX-COMPAT9-AVX: define{{.*}} <2 x i128> @{{.*}}pass_i128_vec{{.*}}(<2 x i128> noundef
// DARWIN-AVX: define{{.*}} <2 x i128> @{{.*}}pass_i128_vec{{.*}}(<2 x i128> noundef
v2u128 pass_i128_vec(v2u128 x) { return x; }
