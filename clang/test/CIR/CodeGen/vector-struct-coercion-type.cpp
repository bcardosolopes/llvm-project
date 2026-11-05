// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG
//
// Vector struct members now correctly preserve their element type
// in ABI coercion (fixed by implementing isSingleElementStruct stripping
// in GetByteVectorType).

typedef float v4f __attribute__((vector_size(16)));
typedef int v4i __attribute__((vector_size(16)));
typedef double v2d __attribute__((vector_size(16)));

// Struct with <4 x float> — should coerce to <4 x float>, not <2 x double>.
struct SV4F { v4f v; };

// CIR-LABEL: define {{.*}} <4 x float> @_Z6f_v4f44SV4F(<4 x float> %
// OGCG-LABEL: define {{.*}} <4 x float> @_Z6f_v4f44SV4F(<4 x float> %
SV4F f_v4f4(SV4F s) { return s; }

// Struct with <4 x i32> — should coerce to <4 x i32>, not <2 x double>.
struct SV4I { v4i v; };

// CIR-LABEL: define {{.*}} <4 x i32> @_Z6f_v4i44SV4I(<4 x i32> %
// OGCG-LABEL: define {{.*}} <4 x i32> @_Z6f_v4i44SV4I(<4 x i32> %
SV4I f_v4i4(SV4I s) { return s; }

// Struct with <2 x double> — should coerce to <2 x double> (correct by coincidence).
struct SV2D { v2d v; };

// CIR-LABEL: define {{.*}} <2 x double> @_Z6f_v2d44SV2D(<2 x double> %
// OGCG-LABEL: define {{.*}} <2 x double> @_Z6f_v2d44SV2D(<2 x double> %
SV2D f_v2d4(SV2D s) { return s; }
