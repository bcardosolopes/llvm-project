// Under the i386 SysV ABI, an MMX-class 64-bit vector whose element is an
// integer narrower than 64 bits (__m64 / <2 x i32> / <4 x i16> / <8 x i8>) is
// coerced to i64 in ARGUMENT position, while the return position keeps the raw
// vector type. <1 x i64> (64-bit integer element) and SSE vectors with a
// floating-point element (<2 x float>) are left un-coerced, and 16-byte vectors
// are unchanged. This matches classic CodeGen (OGCG).

// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -emit-llvm %s -o - | FileCheck --check-prefix=OGCG %s

typedef int __m64 __attribute__((__vector_size__(8)));
typedef short __v4h __attribute__((__vector_size__(8)));
typedef char __v8c __attribute__((__vector_size__(8)));
typedef long long __v1ll __attribute__((__vector_size__(8)));
typedef float __v2f __attribute__((__vector_size__(8)));
typedef int __v4i __attribute__((__vector_size__(16)));

// __m64 (<2 x i32>): arg coerced to i64, return stays <2 x i32>.
// CHECK: define dso_local <2 x i32> @f(i64 noundef {{.*}})
// OGCG:  define dso_local <2 x i32> @f(i64 noundef {{.*}})
__m64 f(__m64 a) { return a; }

// <4 x i16>: arg coerced to i64.
// CHECK: define dso_local <4 x i16> @g(i64 noundef {{.*}})
// OGCG:  define dso_local <4 x i16> @g(i64 noundef {{.*}})
__v4h g(__v4h a) { return a; }

// <8 x i8>: arg coerced to i64.
// CHECK: define dso_local <8 x i8> @h(i64 noundef {{.*}})
// OGCG:  define dso_local <8 x i8> @h(i64 noundef {{.*}})
__v8c h(__v8c a) { return a; }

// <1 x i64>: 64-bit integer element, NOT MMX-coerced.
// CHECK: define dso_local <1 x i64> @i(<1 x i64> noundef {{.*}})
// OGCG:  define dso_local <1 x i64> @i(<1 x i64> noundef {{.*}})
__v1ll i(__v1ll a) { return a; }

// <2 x float>: SSE vector with FP element, NOT MMX-coerced.
// CHECK: define dso_local <2 x float> @j(<2 x float> noundef {{.*}})
// OGCG:  define dso_local <2 x float> @j(<2 x float> noundef {{.*}})
__v2f j(__v2f a) { return a; }

// 16-byte vector: unchanged.
// CHECK: define dso_local <4 x i32> @k(<4 x i32> noundef {{.*}})
// OGCG:  define dso_local <4 x i32> @k(<4 x i32> noundef {{.*}})
__v4i k(__v4i a) { return a; }

// The call site declares/calls f with the coerced i64 argument type too.
// CHECK: call <2 x i32> @f(i64 noundef {{.*}})
// OGCG:  call <2 x i32> @f(i64 noundef {{.*}})
void caller(__m64 *p) { *p = f(*p); }
