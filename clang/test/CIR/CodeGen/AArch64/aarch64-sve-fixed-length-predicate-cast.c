// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve \
// RUN:   -target-feature +sve2 -mvscale-min=2 -mvscale-max=2 -emit-llvm \
// RUN:   -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sve \
// RUN:   -target-feature +sve2 -mvscale-min=2 -mvscale-max=2 -emit-llvm \
// RUN:   %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that converting between a fixed-length svbool_t
// (arm_sve_vector_bits) and the sizeless svbool_t is routed through the
// scalable <vscale x N x i8> predicate carrier via llvm.vector.insert /
// llvm.vector.extract, matching classic CodeGen. A direct bitcast from a
// fixed-length <N x i8> to a scalable <vscale x 16 x i1> predicate is invalid
// and used to crash ClangIR (castIsValid assertion). The predicate carrier for
// the sizeless svbool_t <vscale x 16 x i1> is <vscale x 2 x i8>.
//
// The fixed-length DATA vector path (svint32_t) already went through the
// carrier and is included as a regression check.

typedef __SVBool_t  svbool_t;
typedef __SVInt32_t svint32_t;
typedef svbool_t  fpred __attribute__((arm_sve_vector_bits(256)));
typedef svint32_t fvec  __attribute__((arm_sve_vector_bits(256)));

// Fixed-length predicate -> sizeless predicate.
svbool_t f2s(fpred v) { return v; }
// CIR-LABEL: @f2s(
// CIR: %[[INS:.*]] = call <vscale x 2 x i8> @llvm.vector.insert.nxv2i8.v4i8(<vscale x 2 x i8> {{poison|undef}}, <4 x i8> %{{.*}}, i64 0)
// CIR: bitcast <vscale x 2 x i8> %[[INS]] to <vscale x 16 x i1>
// OGCG-LABEL: @f2s(
// OGCG: %[[INS:.*]] = call <vscale x 2 x i8> @llvm.vector.insert.nxv2i8.v4i8(<vscale x 2 x i8> poison, <4 x i8> %{{.*}}, i64 0)
// OGCG: bitcast <vscale x 2 x i8> %[[INS]] to <vscale x 16 x i1>

// Sizeless predicate -> fixed-length predicate.
fpred s2f(svbool_t v) { return v; }
// CIR-LABEL: @s2f(
// CIR: %[[BC:.*]] = bitcast <vscale x 16 x i1> %{{.*}} to <vscale x 2 x i8>
// CIR: call <4 x i8> @llvm.vector.extract.v4i8.nxv2i8(<vscale x 2 x i8> %[[BC]], i64 0)
// OGCG-LABEL: @s2f(
// OGCG: %[[BC:.*]] = bitcast <vscale x 16 x i1> %{{.*}} to <vscale x 2 x i8>
// OGCG: call <4 x i8> @llvm.vector.extract.v4i8.nxv2i8(<vscale x 2 x i8> %[[BC]], i64 0)

// Regression: fixed-length DATA vectors still convert through the i32 data
// carrier (unchanged), NOT the predicate carrier.
svint32_t df2s(fvec v) { return v; }
// CIR-LABEL: @df2s(
// CIR: call <vscale x 4 x i32> @llvm.vector.insert.nxv4i32.v8i32(<vscale x 4 x i32> {{poison|undef}}, <8 x i32> %{{.*}}, i64 0)
// OGCG-LABEL: @df2s(
// OGCG: call <vscale x 4 x i32> @llvm.vector.insert.nxv4i32.v8i32(<vscale x 4 x i32> poison, <8 x i32> %{{.*}}, i64 0)

fvec ds2f(svint32_t v) { return v; }
// CIR-LABEL: @ds2f(
// CIR: call <8 x i32> @llvm.vector.extract.v8i32.nxv4i32(<vscale x 4 x i32> %{{.*}}, i64 0)
// OGCG-LABEL: @ds2f(
// OGCG: call <8 x i32> @llvm.vector.extract.v8i32.nxv4i32(<vscale x 4 x i32> %{{.*}}, i64 0)
