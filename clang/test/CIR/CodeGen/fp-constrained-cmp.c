// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -ffp-exception-behavior=strict %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -ffp-exception-behavior=strict %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -ffp-exception-behavior=strict %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

typedef float v4f __attribute__((vector_size(16)));

int test_lt(float a, float b) { return a < b; }
// CIR-LABEL: cir.func {{.*}}@test_lt
// CIR: cir.cmp(lt, {{.*}}) : !cir.float, !cir.bool {cir.fp_exception = 2 : i32, cir.fp_signaling_compare}
// LLVM-LABEL: @test_lt
// LLVM: call i1 @llvm.experimental.constrained.fcmps.f32(
// LLVM-SAME: metadata !"olt", metadata !"fpexcept.strict"
// OGCG-LABEL: @test_lt
// OGCG: call i1 @llvm.experimental.constrained.fcmps.f32(
// OGCG-SAME: metadata !"olt", metadata !"fpexcept.strict"

v4f test_vec_lt(v4f a, v4f b) { return a < b; }
// CIR-LABEL: cir.func {{.*}}@test_vec_lt
// CIR: cir.vec.cmp(lt, {{.*}}) : !cir.vector<4 x !cir.float>, !cir.vector<4 x !s32i> {cir.fp_exception = 2 : i32, cir.fp_signaling_compare}
// LLVM-LABEL: @test_vec_lt
// LLVM: call <4 x i1> @llvm.experimental.constrained.fcmps.v4f32(
// LLVM-SAME: metadata !"olt", metadata !"fpexcept.strict"
// OGCG-LABEL: @test_vec_lt
// OGCG: call <4 x i1> @llvm.experimental.constrained.fcmps.v4f32(
// OGCG-SAME: metadata !"olt", metadata !"fpexcept.strict"

v4f test_vec_eq(v4f a, v4f b) { return a == b; }
// CIR-LABEL: cir.func {{.*}}@test_vec_eq
// CIR: cir.vec.cmp(eq, {{.*}}) : !cir.vector<4 x !cir.float>, !cir.vector<4 x !s32i> {cir.fp_exception = 2 : i32}
// LLVM-LABEL: @test_vec_eq
// LLVM: call <4 x i1> @llvm.experimental.constrained.fcmp.v4f32(
// LLVM-SAME: metadata !"oeq", metadata !"fpexcept.strict"
// OGCG-LABEL: @test_vec_eq
// OGCG: call <4 x i1> @llvm.experimental.constrained.fcmp.v4f32(
// OGCG-SAME: metadata !"oeq", metadata !"fpexcept.strict"
