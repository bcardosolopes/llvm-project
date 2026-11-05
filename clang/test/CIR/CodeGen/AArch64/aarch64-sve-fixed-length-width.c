// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve \
// RUN:   -target-feature +sve2 -target-feature +bf16 -mvscale-min=2 \
// RUN:   -mvscale-max=2 -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve \
// RUN:   -target-feature +sve2 -target-feature +bf16 -mvscale-min=2 \
// RUN:   -mvscale-max=2 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that a fixed-length SVE vector (arm_sve_vector_bits(N)) is converted to
// the correct scalable ABI surface type on the function boundary. The scalable
// minimum element count is normalized to the 128-bit SVE granule
// (128 / element-bit-width), independent of N -- it must NOT scale with N.
// Here N = 256 (-mvscale-min/max=2), so the surface type is exactly half as wide
// as the fixed-length element count. Regression test for a ClangIR bug where the
// fixed-length element count (VT.getSize()) was reused as the scalable minimum
// count, producing types N/128 = 2x too wide (e.g. <vscale x 8 x i32> instead of
// <vscale x 4 x i32> for arm_sve_vector_bits(256) svint32_t). Matches OGCG's
// AArch64ABIInfo::convertFixedToScalableVectorType.

typedef __SVInt8_t    svint8_t;
typedef __SVInt16_t   svint16_t;
typedef __SVInt32_t   svint32_t;
typedef __SVInt64_t   svint64_t;
typedef __SVFloat32_t svfloat32_t;
typedef __SVFloat64_t svfloat64_t;
typedef __SVBool_t    svbool_t;

typedef svint8_t    fixed_i8   __attribute__((arm_sve_vector_bits(256)));
typedef svint16_t   fixed_i16  __attribute__((arm_sve_vector_bits(256)));
typedef svint32_t   fixed_i32  __attribute__((arm_sve_vector_bits(256)));
typedef svint64_t   fixed_i64  __attribute__((arm_sve_vector_bits(256)));
typedef svfloat32_t fixed_f32  __attribute__((arm_sve_vector_bits(256)));
typedef svfloat64_t fixed_f64  __attribute__((arm_sve_vector_bits(256)));
typedef svbool_t    fixed_bool __attribute__((arm_sve_vector_bits(256)));

// A fixed-length global keeps its fixed-length in-memory type in both compilers
// (emitted before the functions in module order).
// CIR:  @g = global <8 x i32> zeroinitializer
// OGCG: @g = {{.*}}global <8 x i32> zeroinitializer
fixed_i32 g;

// CIR-LABEL:  define dso_local <vscale x 16 x i8> @idf8(<vscale x 16 x i8>
// OGCG-LABEL: define dso_local <vscale x 16 x i8> @idf8(<vscale x 16 x i8>
fixed_i8 idf8(fixed_i8 x) { return x; }

// CIR-LABEL:  define dso_local <vscale x 8 x i16> @idf16(<vscale x 8 x i16>
// OGCG-LABEL: define dso_local <vscale x 8 x i16> @idf16(<vscale x 8 x i16>
fixed_i16 idf16(fixed_i16 x) { return x; }

// CIR-LABEL:  define dso_local <vscale x 4 x i32> @idf32(<vscale x 4 x i32>
// OGCG-LABEL: define dso_local <vscale x 4 x i32> @idf32(<vscale x 4 x i32>
fixed_i32 idf32(fixed_i32 x) { return x; }

// CIR-LABEL:  define dso_local <vscale x 2 x i64> @idf64(<vscale x 2 x i64>
// OGCG-LABEL: define dso_local <vscale x 2 x i64> @idf64(<vscale x 2 x i64>
fixed_i64 idf64(fixed_i64 x) { return x; }

// CIR-LABEL:  define dso_local <vscale x 4 x float> @idff32(<vscale x 4 x float>
// OGCG-LABEL: define dso_local <vscale x 4 x float> @idff32(<vscale x 4 x float>
fixed_f32 idff32(fixed_f32 x) { return x; }

// CIR-LABEL:  define dso_local <vscale x 2 x double> @idff64(<vscale x 2 x double>
// OGCG-LABEL: define dso_local <vscale x 2 x double> @idff64(<vscale x 2 x double>
fixed_f64 idff64(fixed_f64 x) { return x; }

// A fixed-length predicate always maps to <vscale x 16 x i1>, independent of N.
// CIR-LABEL:  define dso_local <vscale x 16 x i1> @idfb(<vscale x 16 x i1>
// OGCG-LABEL: define dso_local <vscale x 16 x i1> @idfb(<vscale x 16 x i1>
fixed_bool idfb(fixed_bool x) { return x; }
