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

// Verify that a MIXED fixed-length data + fixed-length predicate pure-SVE
// aggregate is passed and returned register-direct (expanded to its scalable
// data + predicate members) per the AAPCS64 pure-scalable-aggregate rule,
// matching classic CodeGen -- not demoted to indirect (sret + byref).
// ClangIR previously demoted the mixed case because the CIR record carries a
// compiler-inserted tail-padding member that the pure-scalable-type
// classification rejected; homogeneous all-data / all-predicate aggregates pack
// with no padding and were already correct.

typedef __SVInt32_t svint32_t;
typedef __SVBool_t  svbool_t;
typedef svint32_t vec  __attribute__((arm_sve_vector_bits(256)));
typedef svbool_t  fpred __attribute__((arm_sve_vector_bits(256)));

// Mixed data + predicate: register-direct <{ <vscale x 4 x i32>, <vscale x 16 x i1> }>.
struct S { vec d; fpred p; };
struct S id(struct S s) { return s; }
// CIR-LABEL: define{{.*}} <{ <vscale x 4 x i32>, <vscale x 16 x i1> }> @id(<vscale x 4 x i32> %{{.*}}, <vscale x 16 x i1> %{{.*}})
// OGCG-LABEL: define{{.*}} <{ <vscale x 4 x i32>, <vscale x 16 x i1> }> @id(<vscale x 4 x i32> %{{.*}}, <vscale x 16 x i1> %{{.*}})

// Reverse member order: predicate then data.
struct S2 { fpred p; vec d; };
struct S2 id2(struct S2 s) { return s; }
// CIR-LABEL: define{{.*}} <{ <vscale x 16 x i1>, <vscale x 4 x i32> }> @id2(<vscale x 16 x i1> %{{.*}}, <vscale x 4 x i32> %{{.*}})
// OGCG-LABEL: define{{.*}} <{ <vscale x 16 x i1>, <vscale x 4 x i32> }> @id2(<vscale x 16 x i1> %{{.*}}, <vscale x 4 x i32> %{{.*}})

// Multiple data + multiple predicate members.
struct S3 { vec d0; vec d1; fpred p0; fpred p1; };
struct S3 id3(struct S3 s) { return s; }
// CIR-LABEL: define{{.*}} <{ <vscale x 4 x i32>, <vscale x 4 x i32>, <vscale x 16 x i1>, <vscale x 16 x i1> }> @id3(<vscale x 4 x i32> %{{.*}}, <vscale x 4 x i32> %{{.*}}, <vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}})
// OGCG-LABEL: define{{.*}} <{ <vscale x 4 x i32>, <vscale x 4 x i32>, <vscale x 16 x i1>, <vscale x 16 x i1> }> @id3(<vscale x 4 x i32> %{{.*}}, <vscale x 4 x i32> %{{.*}}, <vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}})

// Regression: homogeneous all-data aggregate stays a register tuple.
struct AD { vec a; vec b; };
struct AD idad(struct AD s) { return s; }
// CIR-LABEL: define{{.*}} <{ <vscale x 4 x i32>, <vscale x 4 x i32> }> @idad(<vscale x 4 x i32> %{{.*}}, <vscale x 4 x i32> %{{.*}})
// OGCG-LABEL: define{{.*}} <{ <vscale x 4 x i32>, <vscale x 4 x i32> }> @idad(<vscale x 4 x i32> %{{.*}}, <vscale x 4 x i32> %{{.*}})

// Regression: homogeneous all-predicate aggregate stays a register tuple.
struct AP { fpred a; fpred b; };
struct AP idap(struct AP s) { return s; }
// CIR-LABEL: define{{.*}} <{ <vscale x 16 x i1>, <vscale x 16 x i1> }> @idap(<vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}})
// OGCG-LABEL: define{{.*}} <{ <vscale x 16 x i1>, <vscale x 16 x i1> }> @idap(<vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}})

// Regression: a non-pure-SVE struct (int + data vector) stays indirect (sret).
struct MX { int i; vec d; };
struct MX idmx(struct MX s) { return s; }
// CIR-LABEL: define{{.*}} void @idmx(ptr {{.*}}sret(%struct.MX)
// OGCG-LABEL: define{{.*}} void @idmx(ptr {{.*}}sret(%struct.MX)

// Regression: an over-large pure-SVE aggregate (9 data vectors, exceeds the
// 8 SIMD register budget) is still correctly passed indirect.
struct Big { vec d[9]; fpred p; };
struct Big idbig(struct Big s) { return s; }
// CIR-LABEL: define{{.*}} void @idbig(ptr {{.*}}sret(%struct.Big)
// OGCG-LABEL: define{{.*}} void @idbig(ptr {{.*}}sret(%struct.Big)
