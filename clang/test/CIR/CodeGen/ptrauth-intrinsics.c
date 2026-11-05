// RUN: %clang_cc1 %s -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics -fclangir -emit-cir -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 %s -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics -fclangir -emit-llvm -disable-llvm-passes -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 %s -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics -emit-llvm -disable-llvm-passes -o - | FileCheck %s --check-prefix=OGCG

// The explicit __builtin_ptrauth_* builtins map to the llvm.ptrauth.* intrinsic
// family. Classic ref: clang/lib/CodeGen/CGBuiltin.cpp. ClangIR emits the same
// intrinsics via cir.call_llvm_intrinsic: the signed value and pointer-typed
// discriminators are widened to i64, the intrinsic runs on i64 (plus an i32
// immarg key), and the i64 result is cast back to a pointer for
// strip/sign/auth/resign.

void *test_strip(void *p) {
  return __builtin_ptrauth_strip(p, 1);
}
// CIR-LABEL: cir.func {{.*}}@test_strip
// CIR: %[[V:.*]] = cir.cast ptr_to_int {{.*}} -> !u64i
// CIR: %[[R:.*]] = cir.call_llvm_intrinsic "ptrauth.strip" %[[V]], {{.*}} : (!u64i, !s32i) -> !u64i
// CIR: cir.cast int_to_ptr %[[R]] : !u64i -> !cir.ptr<!void>
//
// LLVM-LABEL: define ptr @test_strip
// LLVM: %[[V:.*]] = ptrtoint ptr {{.*}} to i64
// LLVM: %[[R:.*]] = call i64 @llvm.ptrauth.strip(i64 %[[V]], i32 1)
// LLVM: inttoptr i64 %[[R]] to ptr
//
// OGCG-LABEL: define ptr @test_strip
// OGCG: call i64 @llvm.ptrauth.strip(i64 {{.*}}, i32 1)

void *test_sign(void *p, unsigned long d) {
  return __builtin_ptrauth_sign_unauthenticated(p, 2, d);
}
// CIR-LABEL: cir.func {{.*}}@test_sign
// CIR: cir.call_llvm_intrinsic "ptrauth.sign" {{.*}} : (!u64i, !s32i, !u64i) -> !u64i
//
// LLVM-LABEL: define ptr @test_sign
// LLVM: call i64 @llvm.ptrauth.sign(i64 {{.*}}, i32 2, i64 {{.*}})
//
// OGCG-LABEL: define ptr @test_sign
// OGCG: call i64 @llvm.ptrauth.sign(i64 {{.*}}, i32 2, i64 {{.*}})

void *test_auth(void *p, unsigned long d) {
  return __builtin_ptrauth_auth(p, 3, d);
}
// CIR-LABEL: cir.func {{.*}}@test_auth
// CIR: cir.call_llvm_intrinsic "ptrauth.auth" {{.*}} : (!u64i, !s32i, !u64i) -> !u64i
//
// LLVM-LABEL: define ptr @test_auth
// LLVM: call i64 @llvm.ptrauth.auth(i64 {{.*}}, i32 3, i64 {{.*}})
//
// OGCG-LABEL: define ptr @test_auth
// OGCG: call i64 @llvm.ptrauth.auth(i64 {{.*}}, i32 3, i64 {{.*}})

void *test_resign(void *p, unsigned long d1, unsigned long d2) {
  return __builtin_ptrauth_auth_and_resign(p, 0, d1, 1, d2);
}
// CIR-LABEL: cir.func {{.*}}@test_resign
// CIR: cir.call_llvm_intrinsic "ptrauth.resign" {{.*}} : (!u64i, !s32i, !u64i, !s32i, !u64i) -> !u64i
//
// LLVM-LABEL: define ptr @test_resign
// LLVM: call i64 @llvm.ptrauth.resign(i64 {{.*}}, i32 0, i64 {{.*}}, i32 1, i64 {{.*}})
//
// OGCG-LABEL: define ptr @test_resign
// OGCG: call i64 @llvm.ptrauth.resign(i64 {{.*}}, i32 0, i64 {{.*}}, i32 1, i64 {{.*}})

unsigned long test_blend(void *p, unsigned long d) {
  return __builtin_ptrauth_blend_discriminator(p, d);
}
// CIR-LABEL: cir.func {{.*}}@test_blend
// CIR: %[[R:.*]] = cir.call_llvm_intrinsic "ptrauth.blend" {{.*}} : (!u64i, !u64i) -> !u64i
// CIR-NOT: cir.cast int_to_ptr %[[R]]
//
// LLVM-LABEL: define i64 @test_blend
// LLVM: call i64 @llvm.ptrauth.blend(i64 {{.*}}, i64 {{.*}})
//
// OGCG-LABEL: define i64 @test_blend
// OGCG: call i64 @llvm.ptrauth.blend(i64 {{.*}}, i64 {{.*}})

long test_sign_generic(void *p, unsigned long d) {
  return __builtin_ptrauth_sign_generic_data(p, d);
}
// CIR-LABEL: cir.func {{.*}}@test_sign_generic
// CIR: cir.call_llvm_intrinsic "ptrauth.sign.generic" {{.*}} : (!u64i, !u64i) -> !u64i
//
// LLVM-LABEL: define i64 @test_sign_generic
// LLVM: call i64 @llvm.ptrauth.sign.generic(i64 {{.*}}, i64 {{.*}})
//
// OGCG-LABEL: define i64 @test_sign_generic
// OGCG: call i64 @llvm.ptrauth.sign.generic(i64 {{.*}}, i64 {{.*}})
