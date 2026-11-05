// RUN: %clang_cc1 -triple aarch64-none-linux-android24  -fclangir -emit-cir -target-feature +neon %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

struct twoFldT {
  char a, b;
};
// CIR: !rec_anon_struct = !cir.record<struct  {!s64i, !s64i}>
// CIR: !rec_twoFldT = !cir.record<struct "twoFldT" {!s8i, !s8i}

int test_ldrex(char *addr, long long *addr64, float *addrfloat) {
// CIR-LABEL: @test_ldrex
  int sum = 0;
  sum += __builtin_arm_ldrex(addr);
// CIR: [[INTRES0:%.*]] = cir.call_llvm_intrinsic "aarch64.ldxr" {{%[0-9]+}}, {{%[0-9]+}} : (!cir.ptr<!s8i>, !s32i) -> !s64i
// CIR: [[CAST0:%.*]] = cir.cast integral [[INTRES0]] : !s64i -> !s8i
// CIR: [[CAST_I32:%.*]] = cir.cast integral [[CAST0]] : !s8i -> !s32i

  sum += __builtin_arm_ldrex((short *)addr);
// CIR: [[INTRES1:%.*]] = cir.call_llvm_intrinsic "aarch64.ldxr" {{%[0-9]+}}, {{%[0-9]+}} : (!cir.ptr<!s16i>, !s32i) -> !s64i
// CIR: [[CAST1:%.*]] = cir.cast integral [[INTRES1]] : !s64i -> !s16i
// CIR: [[CAST_I16:%.*]] = cir.cast integral [[CAST1]] : !s16i -> !s32i

  sum += __builtin_arm_ldrex((int *)addr);
// CIR: [[INTRES2:%.*]] = cir.call_llvm_intrinsic "aarch64.ldxr" {{%[0-9]+}}, {{%[0-9]+}} : (!cir.ptr<!s32i>, !s32i) -> !s64i
// CIR: [[CAST2:%.*]] = cir.cast integral [[INTRES2]] : !s64i -> !s32i

  sum += __builtin_arm_ldrex((long long *)addr);
// CIR: [[INTRES3:%.*]] = cir.call_llvm_intrinsic "aarch64.ldxr" {{%[0-9]+}}, {{%[0-9]+}} : (!cir.ptr<!s64i>, !s32i) -> !s64i

  sum += __builtin_arm_ldrex(addr64);
// CIR: [[INTRES4:%.*]] = cir.call_llvm_intrinsic "aarch64.ldxr" {{%[0-9]+}}, {{%[0-9]+}} : (!cir.ptr<!s64i>, !s32i) -> !s64i


  sum += *__builtin_arm_ldrex((int **)addr);
// CIR: [[INTRES5:%.*]] = cir.call_llvm_intrinsic "aarch64.ldxr"  {{%[0-9]+}}, {{%[0-9]+}} : (!cir.ptr<!cir.ptr<!s32i>>, !s32i) -> !s64i

  sum += __builtin_arm_ldrex((struct twoFldT **)addr)->a;
// CIR: [[INTRES6:%.*]] = cir.call_llvm_intrinsic "aarch64.ldxr"  {{%[0-9]+}}, {{%[0-9]+}} : (!cir.ptr<!cir.ptr<!rec_twoFldT>>, !s32i) -> !s64i
// CIR: [[CAST3:%.*]] = cir.cast int_to_ptr [[INTRES6]] : !s64i -> !cir.ptr<!rec_twoFldT>
// CIR: [[MEMBER_A:%.*]] = cir.get_member [[CAST3]][0] {name = "a"} : !cir.ptr<!rec_twoFldT> -> !cir.ptr<!s8i>


 // TODO: Uncomment next 2 lines, add tests when floating result type supported
 // sum += __builtin_arm_ldrex(addrfloat);

 // sum += __builtin_arm_ldrex((double *)addr);


  return sum;
}

int test_ldaex(char *addr, long long *addr64, float *addrfloat) {
// CIR-LABEL: @test_ldaex
  int sum = 0;
  sum += __builtin_arm_ldaex(addr);
// CIR: [[INTRES0:%.*]] = cir.call_llvm_intrinsic "aarch64.ldaxr" {{%[0-9]+}}, {{%[0-9]+}} : (!cir.ptr<!s8i>, !s32i) -> !s64i
// CIR: [[CAST0:%.*]] = cir.cast integral [[INTRES0]] : !s64i -> !s8i
// CIR: [[CAST_I32:%.*]] = cir.cast integral [[CAST0]] : !s8i -> !s32i

  sum += __builtin_arm_ldaex((short *)addr);
// CIR: [[INTRES1:%.*]] = cir.call_llvm_intrinsic "aarch64.ldaxr" {{%[0-9]+}}, {{%[0-9]+}} : (!cir.ptr<!s16i>, !s32i) -> !s64i
// CIR: [[CAST1:%.*]] = cir.cast integral [[INTRES1]] : !s64i -> !s16i
// CIR: [[CAST_I16:%.*]] = cir.cast integral [[CAST1]] : !s16i -> !s32i

  sum += __builtin_arm_ldaex((int *)addr);
// CIR: [[INTRES2:%.*]] = cir.call_llvm_intrinsic "aarch64.ldaxr" {{%[0-9]+}}, {{%[0-9]+}} : (!cir.ptr<!s32i>, !s32i) -> !s64i
// CIR: [[CAST2:%.*]] = cir.cast integral [[INTRES2]] : !s64i -> !s32i

  sum += __builtin_arm_ldaex((long long *)addr);
// CIR: [[INTRES3:%.*]] = cir.call_llvm_intrinsic "aarch64.ldaxr" {{%[0-9]+}}, {{%[0-9]+}} : (!cir.ptr<!s64i>, !s32i) -> !s64i

  sum += __builtin_arm_ldaex(addr64);
// CIR: [[INTRES4:%.*]] = cir.call_llvm_intrinsic "aarch64.ldaxr" {{%[0-9]+}}, {{%[0-9]+}} : (!cir.ptr<!s64i>, !s32i) -> !s64i


  sum += *__builtin_arm_ldaex((int **)addr);
// CIR: [[INTRES5:%.*]] = cir.call_llvm_intrinsic "aarch64.ldaxr"  {{%[0-9]+}}, {{%[0-9]+}} : (!cir.ptr<!cir.ptr<!s32i>>, !s32i) -> !s64i

  sum += __builtin_arm_ldaex((struct twoFldT **)addr)->a;
// CIR: [[INTRES6:%.*]] = cir.call_llvm_intrinsic "aarch64.ldaxr"  {{%[0-9]+}}, {{%[0-9]+}} : (!cir.ptr<!cir.ptr<!rec_twoFldT>>, !s32i) -> !s64i
// CIR: [[CAST3:%.*]] = cir.cast int_to_ptr [[INTRES6]] : !s64i -> !cir.ptr<!rec_twoFldT>
// CIR: [[MEMBER_A:%.*]] = cir.get_member [[CAST3]][0] {name = "a"} : !cir.ptr<!rec_twoFldT> -> !cir.ptr<!s8i>


 // TODO: Uncomment next 2 lines, add tests when floating result type supported
 // sum += __builtin_arm_ldaex(addrfloat);

 // sum += __builtin_arm_ldaex((double *)addr);


  return sum;
}

// 128-bit exclusive load/store tests

__int128 test_ldrex_128(__int128 *addr) {
// CIR-LABEL: @test_ldrex_128
  return __builtin_arm_ldrex(addr);
// CIR: [[STRUCTRES:%.*]] = cir.call_llvm_intrinsic "aarch64.ldxp" {{%[0-9]+}} : (!cir.ptr<!s128i>) -> !rec_anon_struct
// CIR: [[HI:%.*]] = cir.extract_member [[STRUCTRES]][1] : !rec_anon_struct -> !s64i
// CIR: [[LO:%.*]] = cir.extract_member [[STRUCTRES]][0] : !rec_anon_struct -> !s64i
// CIR: cir.cast integral [[HI]] : !s64i -> !u64i
// CIR: cir.cast integral {{%.*}} : !u64i -> !u128i
// CIR: cir.cast integral [[LO]] : !s64i -> !u64i
// CIR: cir.cast integral {{%.*}} : !u64i -> !u128i
// CIR: cir.shift(left, {{%.*}} : !u128i, {{%.*}} : !u128i) -> !u128i
// CIR: cir.binop(or, {{%.*}}, {{%.*}}) : !u128i
// CIR: cir.cast bitcast {{%.*}} : !u128i -> !s128i
}

int test_strex_128(__int128 *addr, __int128 val) {
// CIR-LABEL: @test_strex_128
  return __builtin_arm_strex(val, addr);
// CIR: cir.cast bitcast {{%.*}} : !cir.ptr<!s128i> -> !cir.ptr<!rec_anon_struct>
// CIR: cir.load {{.*}} : !cir.ptr<!rec_anon_struct>, !rec_anon_struct
// CIR: cir.extract_member {{%.*}}[0] : !rec_anon_struct -> !s64i
// CIR: cir.extract_member {{%.*}}[1] : !rec_anon_struct -> !s64i
// CIR: cir.call_llvm_intrinsic "aarch64.stxp" {{.*}} : (!s64i, !s64i, !cir.ptr<!s128i>) -> !s32i
}

__int128 test_ldaex_128(__int128 *addr) {
// CIR-LABEL: @test_ldaex_128
  return __builtin_arm_ldaex(addr);
// CIR: [[STRUCTRES:%.*]] = cir.call_llvm_intrinsic "aarch64.ldaxp" {{%[0-9]+}} : (!cir.ptr<!s128i>) -> !rec_anon_struct
// CIR: cir.extract_member [[STRUCTRES]][1] : !rec_anon_struct -> !s64i
// CIR: cir.extract_member [[STRUCTRES]][0] : !rec_anon_struct -> !s64i
}

int test_stlex_128(__int128 *addr, __int128 val) {
// CIR-LABEL: @test_stlex_128
  return __builtin_arm_stlex(val, addr);
// CIR: cir.call_llvm_intrinsic "aarch64.stlxp" {{.*}} : (!s64i, !s64i, !cir.ptr<!s128i>) -> !s32i
}
