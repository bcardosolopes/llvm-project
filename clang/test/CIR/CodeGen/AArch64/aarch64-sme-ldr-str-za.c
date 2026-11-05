// REQUIRES: aarch64-registered-target
//
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sme -target-feature +sme2 -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -target-feature +sme -target-feature +sme2 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that ClangIR lowers the SME ZA vector load/store builtins to the same
// @llvm.aarch64.sme.ldr/.str intrinsics as classic CodeGen. The intrinsics take
// a (slice i32, ptr, vnum-offset i32) signature: the non-vnum forms append a
// constant i32 0, and the _vnum forms truncate the i64 vnum to i32. Tested at
// the raw __builtin_sme_* level to avoid the generated arm_sme.h dependency.

// svldr_za(slice, ptr): non-vnum load appends a constant i32 0 offset.
void test_ldr_za(const void *p, unsigned int slice)
    __arm_streaming_compatible __arm_inout("za") {
  __builtin_sme_svldr_za(slice, p);
}
// CIR-LABEL: @test_ldr_za
// CIR: call void @llvm.aarch64.sme.ldr(i32 {{.*}}, ptr {{.*}}, i32 0)
// OGCG-LABEL: @test_ldr_za
// OGCG: call void @llvm.aarch64.sme.ldr(i32 {{.*}}, ptr {{.*}}, i32 0)

// svstr_za(slice, ptr): non-vnum store appends a constant i32 0 offset.
void test_str_za(void *p, unsigned int slice)
    __arm_streaming_compatible __arm_inout("za") {
  __builtin_sme_svstr_za(slice, p);
}
// CIR-LABEL: @test_str_za
// CIR: call void @llvm.aarch64.sme.str(i32 {{.*}}, ptr {{.*}}, i32 0)
// OGCG-LABEL: @test_str_za
// OGCG: call void @llvm.aarch64.sme.str(i32 {{.*}}, ptr {{.*}}, i32 0)

// svldr_vnum_za(slice, ptr, vnum): _vnum load truncates the i64 vnum to i32.
void test_ldr_vnum_za(const void *p, unsigned int slice, long vnum)
    __arm_streaming_compatible __arm_inout("za") {
  __builtin_sme_svldr_vnum_za(slice, p, vnum);
}
// CIR-LABEL: @test_ldr_vnum_za
// CIR: %[[LV:.*]] = trunc i64 {{.*}} to i32
// CIR: call void @llvm.aarch64.sme.ldr(i32 {{.*}}, ptr {{.*}}, i32 %[[LV]])
// OGCG-LABEL: @test_ldr_vnum_za
// OGCG: %[[LVO:.*]] = trunc i64 {{.*}} to i32
// OGCG: call void @llvm.aarch64.sme.ldr(i32 {{.*}}, ptr {{.*}}, i32 %[[LVO]])

// svstr_vnum_za(slice, ptr, vnum): _vnum store truncates the i64 vnum to i32.
void test_str_vnum_za(void *p, unsigned int slice, long vnum)
    __arm_streaming_compatible __arm_inout("za") {
  __builtin_sme_svstr_vnum_za(slice, p, vnum);
}
// CIR-LABEL: @test_str_vnum_za
// CIR: %[[SV:.*]] = trunc i64 {{.*}} to i32
// CIR: call void @llvm.aarch64.sme.str(i32 {{.*}}, ptr {{.*}}, i32 %[[SV]])
// OGCG-LABEL: @test_str_vnum_za
// OGCG: %[[SVO:.*]] = trunc i64 {{.*}} to i32
// OGCG: call void @llvm.aarch64.sme.str(i32 {{.*}}, ptr {{.*}}, i32 %[[SVO]])
