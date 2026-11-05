// RUN: %clang_cc1 -triple aarch64 -target-feature +sve -mvscale-min=1 -mvscale-max=1 -fclangir -emit-mlir=cir-abi -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple aarch64 -target-feature +sve -mvscale-min=1 -mvscale-max=1 -O3 -fclangir -emit-mlir=cir-abi -o %t.opt.cir %s
// RUN: FileCheck --input-file=%t.opt.cir --check-prefix=CIR-LIFETIMES %s
// RUN: %clang_cc1 -triple aarch64 -target-feature +sve -mvscale-min=1 -mvscale-max=1 -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple aarch64 -target-feature +sve -mvscale-min=1 -mvscale-max=1 -O3 -disable-llvm-passes -fclangir -emit-llvm -o %t.opt.ll %s
// RUN: FileCheck --input-file=%t.opt.ll --check-prefix=LLVM-LIFETIMES %s

// REQUIRES: aarch64-registered-target

#include <arm_sve.h>

typedef svfloat32_t fvec32 __attribute__((arm_sve_vector_bits(128)));
typedef svfloat64_t fvec64 __attribute__((arm_sve_vector_bits(128)));
typedef svbool_t bvec __attribute__((arm_sve_vector_bits(128)));

// Pure Scalable Type with a single SVE vector member.
typedef struct {
    fvec32 x;
} SmallPST;

// Pure Scalable Type with two SVE vector members.
typedef struct {
    fvec32 a;
    fvec64 b;
} MultiPST;

// Pure Scalable Type with ABI padding between predicate and vector members.
typedef struct {
    bvec a;
    fvec64 x;
    fvec32 y[2];
    bvec b;
} PaddedPST;

void use(SmallPST *);
void use_multi(MultiPST *);

// Test CoerceAndExpand in function prolog: PST arg is expanded to individual
// vector parameter and reconstructed into the struct in the function body.
void test_pst_arg(SmallPST s) {
    use(&s);
}
// CIR: cir.func {{.*}} @test_pst_arg(%arg0: !cir.vector<[4] x !cir.float>
// CIR:   %[[ALLOCA:.*]] = cir.alloca !rec_SmallPST
// CIR:   cir.cast bitcast %[[ALLOCA]]
// CIR:   %[[UNDEF:.*]] = cir.const #cir.undef : !rec_anon_struct
// CIR:   %[[ZERO:.*]] = cir.const #cir.int<0> : !u64i
// CIR:   %[[FIXED:.*]] = cir.call_llvm_intrinsic "vector.extract" %arg0, %[[ZERO]] : (!cir.vector<[4] x !cir.float>, !u64i) -> !cir.vector<4 x !cir.float, sve_fixed>
// CIR:   %[[REC:.*]] = cir.insert_member %[[UNDEF]][0], %[[FIXED]] : !rec_anon_struct, !cir.vector<4 x !cir.float, sve_fixed>
// CIR:   cir.store {{.*}} %[[REC]],

// LLVM: define dso_local void @test_pst_arg(<vscale x 4 x float> %0)
// LLVM:   alloca %struct.SmallPST
// LLVM:   %[[EXTRACT:.*]] = call <4 x float> @llvm.vector.extract.v4f32.nxv4f32(<vscale x 4 x float> %0, i64 0)
// LLVM:   %[[REC:.*]] = insertvalue { <4 x float> } undef, <4 x float> %[[EXTRACT]], 0
// LLVM:   store { <4 x float> } %[[REC]], ptr %{{.*}}, align 16

// Test CoerceAndExpand in call return and call arg rewriting.
SmallPST test_pst_ret(void);
void test_pst_call(void) {
    SmallPST s = test_pst_ret();
    use(&s);
}
// CIR: cir.func {{.*}} @test_pst_call
// CIR:   %[[RET:.*]] = cir.call @test_pst_ret() : () -> !cir.vector<[4] x !cir.float>
// CIR:   %[[UNDEF:.*]] = cir.const #cir.undef : !rec_anon_struct
// CIR:   %[[ZERO:.*]] = cir.const #cir.int<0> : !u64i
// CIR:   %[[FIXED:.*]] = cir.call_llvm_intrinsic "vector.extract" %[[RET]], %[[ZERO]] : (!cir.vector<[4] x !cir.float>, !u64i) -> !cir.vector<4 x !cir.float, sve_fixed>
// CIR:   %[[REC:.*]] = cir.insert_member %[[UNDEF]][0], %[[FIXED]]
// CIR:   cir.store align(16) %[[REC]], %{{.*}} : !rec_anon_struct, !cir.ptr<!rec_anon_struct>

// LLVM: define dso_local void @test_pst_call()
// LLVM:   %[[RET:.*]] = call <vscale x 4 x float> @test_pst_ret()
// LLVM:   %[[FIXED:.*]] = call <4 x float> @llvm.vector.extract.v4f32.nxv4f32(<vscale x 4 x float> %[[RET]], i64 0)
// LLVM:   %[[REC:.*]] = insertvalue { <4 x float> } undef, <4 x float> %[[FIXED]], 0
// LLVM:   store { <4 x float> } %[[REC]], ptr %{{.*}}, align 16

// Test multi-member PST: two SVE vectors expanded into separate parameters.
void test_multi_pst_arg(MultiPST s) {
    use_multi(&s);
}
// CIR: cir.func {{.*}} @test_multi_pst_arg(%arg0: !cir.vector<[4] x !cir.float> {{.*}}, %arg1: !cir.vector<[2] x !cir.double>
// CIR:   %[[ALLOCA2:.*]] = cir.alloca !rec_MultiPST
// CIR:   %[[UNDEF:.*]] = cir.const #cir.undef : !rec_anon_struct1
// CIR:   cir.call_llvm_intrinsic "vector.extract" %arg0
// CIR:   %[[REC0:.*]] = cir.insert_member %[[UNDEF]][0]
// CIR:   cir.call_llvm_intrinsic "vector.extract" %arg1
// CIR:   %[[REC1:.*]] = cir.insert_member %[[REC0]][1]
// CIR:   cir.store {{.*}} %[[REC1]],

// LLVM: define dso_local void @test_multi_pst_arg(<vscale x 4 x float> %0, <vscale x 2 x double> %1)
// LLVM:   alloca %struct.MultiPST
// LLVM:   %[[FIXED0:.*]] = call <4 x float> @llvm.vector.extract.v4f32.nxv4f32(<vscale x 4 x float> %0, i64 0)
// LLVM:   %[[REC0:.*]] = insertvalue { <4 x float>, <2 x double> } undef, <4 x float> %[[FIXED0]], 0
// LLVM:   %[[FIXED1:.*]] = call <2 x double> @llvm.vector.extract.v2f64.nxv2f64(<vscale x 2 x double> %1, i64 0)
// LLVM:   %[[REC1:.*]] = insertvalue { <4 x float>, <2 x double> } %[[REC0]], <2 x double> %[[FIXED1]], 1
// LLVM:   store { <4 x float>, <2 x double> } %[[REC1]], ptr %{{.*}}, align 16

// Test multi-member PST call return.
MultiPST test_multi_ret(void);
void test_multi_pst_call(void) {
    MultiPST s = test_multi_ret();
    use_multi(&s);
}
// CIR: cir.func {{.*}} @test_multi_pst_call
// CIR:   %[[RET:.*]] = cir.call @test_multi_ret() : () -> !rec_anon_struct2
// CIR:   %[[UNDEF:.*]] = cir.const #cir.undef : !rec_anon_struct1
// CIR:   %[[F0:.*]] = cir.extract_member %[[RET]][0] : !rec_anon_struct2 -> !cir.vector<[4] x !cir.float>
// CIR:   %[[FIXED0:.*]] = cir.call_llvm_intrinsic "vector.extract" %[[F0]]
// CIR:   %[[REC0:.*]] = cir.insert_member %[[UNDEF]][0], %[[FIXED0]]
// CIR:   %[[F1:.*]] = cir.extract_member %[[RET]][1] : !rec_anon_struct2 -> !cir.vector<[2] x !cir.double>
// CIR:   %[[FIXED1:.*]] = cir.call_llvm_intrinsic "vector.extract" %[[F1]]
// CIR:   %[[REC1:.*]] = cir.insert_member %[[REC0]][1], %[[FIXED1]]
// CIR:   cir.store align(16) %[[REC1]], %{{.*}} : !rec_anon_struct1, !cir.ptr<!rec_anon_struct1>

// LLVM: define dso_local void @test_multi_pst_call()
// LLVM:   %[[RET:.*]] = call <{ <vscale x 4 x float>, <vscale x 2 x double> }> @test_multi_ret()
// LLVM:   %[[F0:.*]] = extractvalue <{ <vscale x 4 x float>, <vscale x 2 x double> }> %[[RET]], 0
// LLVM:   %[[FIXED0:.*]] = call <4 x float> @llvm.vector.extract.v4f32.nxv4f32(<vscale x 4 x float> %[[F0]], i64 0)
// LLVM:   %[[REC0:.*]] = insertvalue { <4 x float>, <2 x double> } undef, <4 x float> %[[FIXED0]], 0
// LLVM:   %[[F1:.*]] = extractvalue <{ <vscale x 4 x float>, <vscale x 2 x double> }> %[[RET]], 1
// LLVM:   %[[FIXED1:.*]] = call <2 x double> @llvm.vector.extract.v2f64.nxv2f64(<vscale x 2 x double> %[[F1]], i64 0)
// LLVM:   %[[REC1:.*]] = insertvalue { <4 x float>, <2 x double> } %[[REC0]], <2 x double> %[[FIXED1]], 1
// LLVM:   store { <4 x float>, <2 x double> } %[[REC1]], ptr %{{.*}}, align 16

// Test call arg rewriting for a PST with ABI padding between fields: direct-CIR
// should load the coerced members directly instead of round-tripping through a
// temporary aggregate, and the non-zero-offset fields must keep align(16).
void padded_pst_callee(PaddedPST);
void test_padded_pst_arg(PaddedPST *p) {
    padded_pst_callee(*p);
}
// CIR-LIFETIMES: cir.func {{.*}} @test_padded_pst_arg(%arg0: !cir.ptr<!rec_PaddedPST>
// CIR-LIFETIMES:   %[[SRC:.*]] = cir.load deref align(8) %{{.*}} : !cir.ptr<!cir.ptr<!rec_PaddedPST>>, !cir.ptr<!rec_PaddedPST>
// CIR-LIFETIMES:   %[[COERCE:.*]] = cir.cast bitcast %[[SRC]] : !cir.ptr<!rec_PaddedPST> -> !cir.ptr<!rec_anon_struct{{[0-9]*}}>
// CIR-LIFETIMES-NOT: cir.alloca !rec_anon_struct{{[0-9]*}}
// CIR-LIFETIMES-NOT: cir.store %{{.*}} : !rec_anon_struct{{[0-9]*}}, !cir.ptr<!rec_anon_struct{{[0-9]*}}>
// CIR-LIFETIMES:   %[[X_PTR:.*]] = cir.get_member %[[COERCE]][1] {name = ""} : !cir.ptr<!rec_anon_struct{{[0-9]*}}> -> !cir.ptr<!cir.vector<2 x !cir.double, sve_fixed>>
// CIR-LIFETIMES-NEXT:   %[[X:.*]] = cir.load align(16) %[[X_PTR]] : !cir.ptr<!cir.vector<2 x !cir.double, sve_fixed>>, !cir.vector<2 x !cir.double, sve_fixed>
// CIR-LIFETIMES:   %[[B_PTR:.*]] = cir.get_member %[[COERCE]][4] {name = ""} : !cir.ptr<!rec_anon_struct{{[0-9]*}}> -> !cir.ptr<!cir.vector<2 x !u8i, sve_fixed>>
// CIR-LIFETIMES-NEXT:   %[[B:.*]] = cir.load align(16) %[[B_PTR]] : !cir.ptr<!cir.vector<2 x !u8i, sve_fixed>>, !cir.vector<2 x !u8i, sve_fixed>
// CIR-LIFETIMES:   cir.call @padded_pst_callee(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}) : (!cir.vector<[16] x !cir.bool>, !cir.vector<[2] x !cir.double>, !cir.vector<[4] x !cir.float>, !cir.vector<[4] x !cir.float>, !cir.vector<[16] x !cir.bool>) -> ()

// LLVM-LIFETIMES: define dso_local void @test_padded_pst_arg(ptr noundef %0)
// LLVM-LIFETIMES-NOT: alloca { <2 x i8>, <2 x double>, <4 x float>, <4 x float>, <2 x i8> }
// LLVM-LIFETIMES:   %[[SRC:[0-9]+]] = load ptr, ptr %{{[0-9]+}}, align 8
// LLVM-LIFETIMES:   %[[X_PTR:[0-9]+]] = getelementptr inbounds nuw { <2 x i8>, <2 x double>, <4 x float>, <4 x float>, <2 x i8> }, ptr %[[SRC]], i32 0, i32 1
// LLVM-LIFETIMES-NEXT:   %[[X:[0-9]+]] = load <2 x double>, ptr %[[X_PTR]], align 16
// LLVM-LIFETIMES:   %[[B_PTR:[0-9]+]] = getelementptr inbounds nuw { <2 x i8>, <2 x double>, <4 x float>, <4 x float>, <2 x i8> }, ptr %[[SRC]], i32 0, i32 4
// LLVM-LIFETIMES-NEXT:   %[[B:[0-9]+]] = load <2 x i8>, ptr %[[B_PTR]], align 16
// LLVM-LIFETIMES:   call void @padded_pst_callee(<vscale x 16 x i1> %{{.*}}, <vscale x 2 x double> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 16 x i1> %{{.*}})

// Test returning a PST from a function body (CoerceAndExpand epilog).
SmallPST make_pst(void) {
    SmallPST s;
    return s;
}
// CIR: cir.func {{.*}} @make_pst() -> !cir.vector<[4] x !cir.float>
// CIR:   %[[LOADREC:.*]] = cir.load align(16) %{{.*}} : !cir.ptr<!rec_anon_struct>, !rec_anon_struct
// CIR:   %[[FIXED:.*]] = cir.extract_member %[[LOADREC]][0] : !rec_anon_struct -> !cir.vector<4 x !cir.float, sve_fixed>
// CIR:   %[[ZERO:.*]] = cir.const #cir.int<0> : !u64i
// CIR:   %[[POISON:.*]] = cir.const #cir.poison : !cir.vector<[4] x !cir.float>
// CIR:   %[[SCALABLE:.*]] = cir.call_llvm_intrinsic "vector.insert" %[[POISON]], %[[FIXED]], %[[ZERO]] : (!cir.vector<[4] x !cir.float>, !cir.vector<4 x !cir.float, sve_fixed>, !u64i) -> !cir.vector<[4] x !cir.float>
// CIR:   cir.return %[[SCALABLE]] : !cir.vector<[4] x !cir.float>

// LLVM: define dso_local <vscale x 4 x float> @make_pst()
// LLVM:   %[[RET:.*]] = call <vscale x 4 x float> @llvm.vector.insert.nxv4f32.v4f32(<vscale x 4 x float> poison, <4 x float> %{{.*}}, i64 0)
// LLVM:   ret <vscale x 4 x float> %[[RET]]

// Test direct SVE fixed-length vector argument (not in a struct).
void use_vec(fvec32 *);
void test_sve_direct_arg(fvec32 v) {
    use_vec(&v);
}
// CIR: cir.func {{.*}} @test_sve_direct_arg(%arg0: !cir.vector<[4] x !cir.float>
// CIR:   cir.call_llvm_intrinsic "vector.extract" %arg0
// LLVM: define dso_local void @test_sve_direct_arg(<vscale x 4 x float> noundef %0)

// Test PST register overflow: 5 × MultiPST (2 vectors each = 10 total).
// First 4 args consume 8 SIMD registers, the 5th must go indirect.
void test_pst_overflow(MultiPST a, MultiPST b, MultiPST c, MultiPST d,
                       MultiPST e) {
    use_multi(&e);
}
// The first four args are expanded (CoerceAndExpand), the fifth is indirect (ptr).
// CIR: cir.func {{.*}} @test_pst_overflow(
// CIR-SAME: %arg0: !cir.vector<[4] x !cir.float>
// CIR-SAME: %arg1: !cir.vector<[2] x !cir.double>
// CIR-SAME: %arg2: !cir.vector<[4] x !cir.float>
// CIR-SAME: %arg3: !cir.vector<[2] x !cir.double>
// CIR-SAME: %arg4: !cir.vector<[4] x !cir.float>
// CIR-SAME: %arg5: !cir.vector<[2] x !cir.double>
// CIR-SAME: %arg6: !cir.vector<[4] x !cir.float>
// CIR-SAME: %arg7: !cir.vector<[2] x !cir.double>
// CIR-SAME: %arg8: !cir.ptr<!rec_MultiPST>

// LLVM: define dso_local void @test_pst_overflow(
// LLVM-SAME: <vscale x 4 x float> %0, <vscale x 2 x double> %1,
// LLVM-SAME: <vscale x 4 x float> %2, <vscale x 2 x double> %3,
// LLVM-SAME: <vscale x 4 x float> %4, <vscale x 2 x double> %5,
// LLVM-SAME: <vscale x 4 x float> %6, <vscale x 2 x double> %7,
// LLVM-SAME: ptr dead_on_return noundef %8)

// SVE tuples should also participate in AAPCS64 pure-scalable expansion when
// mixed with PSTs, instead of being lowered indirectly.
void tuple_pst_callee(svfloat64x4_t, MultiPST);
void test_tuple_pst_arg(MultiPST *p, svfloat64x4_t x) {
    tuple_pst_callee(x, *p);
}
// CIR: cir.func {{.*}} @test_tuple_pst_arg(%arg0: !cir.ptr<!rec_MultiPST> {{.*}}, %arg1: !cir.vector<[2] x !cir.double> {{.*}}, %arg2: !cir.vector<[2] x !cir.double> {{.*}}, %arg3: !cir.vector<[2] x !cir.double> {{.*}}, %arg4: !cir.vector<[2] x !cir.double>
// CIR:   cir.call @tuple_pst_callee(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}) : (!cir.vector<[2] x !cir.double>, !cir.vector<[2] x !cir.double>, !cir.vector<[2] x !cir.double>, !cir.vector<[2] x !cir.double>, !cir.vector<[4] x !cir.float>, !cir.vector<[2] x !cir.double>) -> ()
// CIR: cir.func private @tuple_pst_callee(!cir.vector<[2] x !cir.double>, !cir.vector<[2] x !cir.double>, !cir.vector<[2] x !cir.double>, !cir.vector<[2] x !cir.double>, !cir.vector<[4] x !cir.float>, !cir.vector<[2] x !cir.double>)
// LLVM: define dso_local void @test_tuple_pst_arg(ptr noundef %0, <vscale x 2 x double> %1, <vscale x 2 x double> %2, <vscale x 2 x double> %3, <vscale x 2 x double> %4)
// LLVM:   call void @tuple_pst_callee(<vscale x 2 x double> %{{.*}}, <vscale x 2 x double> %{{.*}}, <vscale x 2 x double> %{{.*}}, <vscale x 2 x double> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 2 x double> %{{.*}})
// LLVM: declare void @tuple_pst_callee(<vscale x 2 x double>, <vscale x 2 x double>, <vscale x 2 x double>, <vscale x 2 x double>, <vscale x 4 x float>, <vscale x 2 x double>)

// Tuples should stay direct while registers are still available.
void test_tuple_reg_count(svfloat32_t x, svfloat32x2_t y) {
    void test_tuple_reg_count_callee(svfloat32_t, svfloat32_t, svfloat32_t,
                                     svfloat32_t, svfloat32_t, svfloat32_t,
                                     svfloat32_t, svfloat32x2_t);
    test_tuple_reg_count_callee(x, x, x, x, x, x, x, y);
}
// CIR: cir.func {{.*}} @test_tuple_reg_count(%arg0: !cir.vector<[4] x !cir.float> {{.*}}, %arg1: !cir.vector<[4] x !cir.float> {{.*}}, %arg2: !cir.vector<[4] x !cir.float>
// CIR:   cir.call @test_tuple_reg_count_callee(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}) : (!cir.vector<[4] x !cir.float>, !cir.vector<[4] x !cir.float>, !cir.vector<[4] x !cir.float>, !cir.vector<[4] x !cir.float>, !cir.vector<[4] x !cir.float>, !cir.vector<[4] x !cir.float>, !cir.vector<[4] x !cir.float>, {{.*}}) -> ()
// LLVM: define dso_local void @test_tuple_reg_count(<vscale x 4 x float> %0, <vscale x 4 x float> %1, <vscale x 4 x float> %2)
// LLVM:   call void @test_tuple_reg_count_callee(<vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, ptr dead_on_return noundef %{{.*}})
// LLVM: declare void @test_tuple_reg_count_callee(<vscale x 4 x float>, <vscale x 4 x float>, <vscale x 4 x float>, <vscale x 4 x float>, <vscale x 4 x float>, <vscale x 4 x float>, <vscale x 4 x float>, ptr dead_on_return noundef)
// CIR-LIFETIMES: cir.func {{.*}} @test_tuple_reg_count(%arg0: !cir.vector<[4] x !cir.float> {{.*}}, %arg1: !cir.vector<[4] x !cir.float> {{.*}}, %arg2: !cir.vector<[4] x !cir.float>
// CIR-LIFETIMES:   %[[TUPLE_TMP:.*]] = cir.alloca {{.*}}["coerce"] {alignment = 16 : i64} {{.*}}
// CIR-LIFETIMES-NEXT:   cir.lifetime.start %[[TUPLE_TMP]] : {{.*}}
// CIR-LIFETIMES:   cir.call @test_tuple_reg_count_callee(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %[[TUPLE_TMP]]) : (!cir.vector<[4] x !cir.float>, !cir.vector<[4] x !cir.float>, !cir.vector<[4] x !cir.float>, !cir.vector<[4] x !cir.float>, !cir.vector<[4] x !cir.float>, !cir.vector<[4] x !cir.float>, !cir.vector<[4] x !cir.float>, {{.*}}) -> ()
// CIR-LIFETIMES-NEXT:   cir.lifetime.end %[[TUPLE_TMP]] : {{.*}}
// LLVM-LIFETIMES: define dso_local void @test_tuple_reg_count(<vscale x 4 x float> %0, <vscale x 4 x float> %1, <vscale x 4 x float> %2)
// LLVM-LIFETIMES:   call void @llvm.lifetime.start.p0(ptr %[[TUPLE_TMP:[0-9]+]])
// LLVM-LIFETIMES:   call void @test_tuple_reg_count_callee(<vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, <vscale x 4 x float> %{{.*}}, ptr dead_on_return noundef %[[TUPLE_TMP]])
// LLVM-LIFETIMES-NEXT:   call void @llvm.lifetime.end.p0(ptr %[[TUPLE_TMP]])

// Predicate tuples should use predicate registers until they are exhausted,
// then spill the remaining tuple indirectly.
void test_tuple_reg_count_bool(svboolx4_t x, svboolx4_t y) {
    void test_tuple_reg_count_bool_callee(svboolx4_t, svboolx4_t);
    test_tuple_reg_count_bool_callee(x, y);
}
// CIR: cir.func {{.*}} @test_tuple_reg_count_bool(%arg0: !cir.vector<[16] x !cir.bool> {{.*}}, %arg1: !cir.vector<[16] x !cir.bool> {{.*}}, %arg2: !cir.vector<[16] x !cir.bool> {{.*}}, %arg3: !cir.vector<[16] x !cir.bool> {{.*}}, %arg4: !cir.ptr<!rec_anon_struct{{[0-9]+}}>
// CIR:   cir.call @test_tuple_reg_count_bool_callee(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}) : (!cir.vector<[16] x !cir.bool>, !cir.vector<[16] x !cir.bool>, !cir.vector<[16] x !cir.bool>, !cir.vector<[16] x !cir.bool>, {{.*}}) -> ()
// LLVM: define dso_local void @test_tuple_reg_count_bool(<vscale x 16 x i1> %0, <vscale x 16 x i1> %1, <vscale x 16 x i1> %2, <vscale x 16 x i1> %3, ptr dead_on_return noundef %4)
// LLVM:   %[[FINAL_TMP:.*]] = alloca { <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1> }, i64 1, align 2
// LLVM:   store { <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1> } %{{.*}}, ptr %[[FINAL_TMP]], align 2
// LLVM:   %[[ARGPTR:.*]] = load ptr, ptr %{{.*}}, align 8
// LLVM:   store { <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1> } %{{.*}}, ptr %[[SPILL:.*]], align 2
// LLVM:   %[[REC:.*]] = load { <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1> }, ptr %[[SPILL]], align 2
// LLVM-NEXT:   %[[X0:.*]] = extractvalue { <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1> } %[[REC]], 0
// LLVM:   call void @test_tuple_reg_count_bool_callee(<vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}}, ptr dead_on_return noundef %{{.*}})
// LLVM: declare void @test_tuple_reg_count_bool_callee(<vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>, ptr dead_on_return noundef)
// CIR-LIFETIMES: cir.func {{.*}} @test_tuple_reg_count_bool(%arg0: !cir.vector<[16] x !cir.bool> {{.*}}, %arg1: !cir.vector<[16] x !cir.bool> {{.*}}, %arg2: !cir.vector<[16] x !cir.bool> {{.*}}, %arg3: !cir.vector<[16] x !cir.bool> {{.*}}, %arg4: !cir.ptr<!rec_anon_struct{{[0-9]+}}>
// CIR-LIFETIMES:   %[[NONFINAL_TMP:.*]] = cir.alloca {{.*}}["coerce"] {alignment = 2 : i64} {{.*}}
// CIR-LIFETIMES:   %[[FINAL_TMP:.*]] = cir.alloca {{.*}}["coerce"] {alignment = 2 : i64} {{.*}}
// CIR-LIFETIMES-NEXT:   cir.lifetime.start %[[FINAL_TMP]] : {{.*}}
// CIR-LIFETIMES:   cir.call @test_tuple_reg_count_bool_callee(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %[[FINAL_TMP]]) : (!cir.vector<[16] x !cir.bool>, !cir.vector<[16] x !cir.bool>, !cir.vector<[16] x !cir.bool>, !cir.vector<[16] x !cir.bool>, {{.*}}) -> ()
// CIR-LIFETIMES-NEXT:   cir.lifetime.end %[[FINAL_TMP]] : {{.*}}
// LLVM-LIFETIMES: define dso_local void @test_tuple_reg_count_bool(<vscale x 16 x i1> %0, <vscale x 16 x i1> %1, <vscale x 16 x i1> %2, <vscale x 16 x i1> %3, ptr dead_on_return noundef %4)
// LLVM-LIFETIMES:   call void @llvm.lifetime.start.p0(ptr %[[FINAL_TMP:[0-9]+]])
// LLVM-LIFETIMES:   store { <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1>, <vscale x 16 x i1> } %{{.*}}, ptr %[[FINAL_TMP]], align 2
// LLVM-LIFETIMES:   call void @test_tuple_reg_count_bool_callee(<vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}}, <vscale x 16 x i1> %{{.*}}, ptr dead_on_return noundef %[[FINAL_TMP]])
// LLVM-LIFETIMES-NEXT:   call void @llvm.lifetime.end.p0(ptr %[[FINAL_TMP]])
