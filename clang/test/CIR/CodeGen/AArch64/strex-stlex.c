// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

int test_strex(int val, int *addr) {
  return __builtin_arm_strex(val, addr);
}

// CIR: define {{.*}}i32 @test_strex(i32 {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// CIR:   %[[VAL:.*]] = zext i32 %{{.*}} to i64
// CIR:   %[[RES:.*]] = call i32 @llvm.aarch64.stxr.p0(i64 %[[VAL]], ptr elementtype(i32) %{{.*}})
// CIR:   ret i32

// OGCG: define {{.*}}i32 @test_strex(i32 {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// OGCG:   %[[VAL:.*]] = zext i32 %{{.*}} to i64
// OGCG:   %[[RES:.*]] = call i32 @llvm.aarch64.stxr.p0(i64 %[[VAL]], ptr elementtype(i32) %{{.*}})

int test_stlex(int val, int *addr) {
  return __builtin_arm_stlex(val, addr);
}

// CIR: define {{.*}}i32 @test_stlex(i32 {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// CIR:   %[[VAL:.*]] = zext i32 %{{.*}} to i64
// CIR:   %[[RES:.*]] = call i32 @llvm.aarch64.stlxr.p0(i64 %[[VAL]], ptr elementtype(i32) %{{.*}})
// CIR:   ret i32

// OGCG: define {{.*}}i32 @test_stlex(i32 {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// OGCG:   %[[VAL:.*]] = zext i32 %{{.*}} to i64
// OGCG:   %[[RES:.*]] = call i32 @llvm.aarch64.stlxr.p0(i64 %[[VAL]], ptr elementtype(i32) %{{.*}})

int test_strex_short(short val, short *addr) {
  return __builtin_arm_strex(val, addr);
}

// CIR: define {{.*}}i32 @test_strex_short(i16 {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// CIR:   %[[VAL:.*]] = zext i16 %{{.*}} to i64
// CIR:   %[[RES:.*]] = call i32 @llvm.aarch64.stxr.p0(i64 %[[VAL]], ptr elementtype(i16) %{{.*}})
// CIR:   ret i32

// OGCG: define {{.*}}i32 @test_strex_short(i16 {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
// OGCG:   %[[VAL:.*]] = zext i16 %{{.*}} to i64
// OGCG:   %[[RES:.*]] = call i32 @llvm.aarch64.stxr.p0(i64 %[[VAL]], ptr elementtype(i16) %{{.*}})

int test_ldrex(int *addr) {
  return __builtin_arm_ldrex(addr);
}

// CIR: define {{.*}}i32 @test_ldrex(ptr {{.*}}%{{.*}})
// CIR:   call i64 @llvm.aarch64.ldxr.p0(ptr elementtype(i32) %{{.*}})

// OGCG: define {{.*}}i32 @test_ldrex(ptr {{.*}}%{{.*}})
// OGCG:   call i64 @llvm.aarch64.ldxr.p0(ptr elementtype(i32) %{{.*}})

int test_ldaex(int *addr) {
  return __builtin_arm_ldaex(addr);
}

// CIR: define {{.*}}i32 @test_ldaex(ptr {{.*}}%{{.*}})
// CIR:   call i64 @llvm.aarch64.ldaxr.p0(ptr elementtype(i32) %{{.*}})

// OGCG: define {{.*}}i32 @test_ldaex(ptr {{.*}}%{{.*}})
// OGCG:   call i64 @llvm.aarch64.ldaxr.p0(ptr elementtype(i32) %{{.*}})

// 128-bit exclusive load/store tests

__int128 test_ldrex_128(__int128 *addr) {
  return __builtin_arm_ldrex(addr);
}

// CIR: define {{.*}}i128 @test_ldrex_128(ptr {{.*}}%{{.*}})
// CIR:   [[STRUCTRES:%.*]] = call { i64, i64 } @llvm.aarch64.ldxp(ptr %{{.*}})
// CIR:   [[RESHI:%.*]] = extractvalue { i64, i64 } [[STRUCTRES]], 1
// CIR:   [[RESLO:%.*]] = extractvalue { i64, i64 } [[STRUCTRES]], 0
// CIR:   [[RESHI128:%.*]] = zext i64 [[RESHI]] to i128
// CIR:   [[RESLO128:%.*]] = zext i64 [[RESLO]] to i128
// CIR:   [[SHIFTED:%.*]] = shl i128 [[RESHI128]], 64
// CIR:   [[INTRES:%.*]] = or i128 [[SHIFTED]], [[RESLO128]]

// OGCG: define {{.*}}i128 @test_ldrex_128(ptr {{.*}}%{{.*}})
// OGCG:   [[STRUCTRES:%.*]] = call { i64, i64 } @llvm.aarch64.ldxp(ptr %{{.*}})
// OGCG:   [[RESHI:%.*]] = extractvalue { i64, i64 } [[STRUCTRES]], 1
// OGCG:   [[RESLO:%.*]] = extractvalue { i64, i64 } [[STRUCTRES]], 0
// OGCG:   [[RESHI128:%.*]] = zext i64 [[RESHI]] to i128
// OGCG:   [[RESLO128:%.*]] = zext i64 [[RESLO]] to i128
// OGCG:   shl nuw i128 [[RESHI128]], 64

int test_strex_128(__int128 *addr, __int128 val) {
  return __builtin_arm_strex(val, addr);
}

// CIR: define {{.*}}i32 @test_strex_128(ptr {{.*}}%{{.*}}, i128 {{.*}}%{{.*}})
// CIR:   [[LOHI:%.*]] = load { i64, i64 }, ptr %{{.*}}
// CIR:   [[LO:%.*]] = extractvalue { i64, i64 } [[LOHI]], 0
// CIR:   [[HI:%.*]] = extractvalue { i64, i64 } [[LOHI]], 1
// CIR:   call i32 @llvm.aarch64.stxp(i64 [[LO]], i64 [[HI]], ptr %{{.*}})

// OGCG: define {{.*}}i32 @test_strex_128(ptr {{.*}}%{{.*}}, i128 {{.*}}%{{.*}})
// OGCG:   [[LOHI:%.*]] = load { i64, i64 }, ptr %{{.*}}
// OGCG:   [[LO:%.*]] = extractvalue { i64, i64 } [[LOHI]], 0
// OGCG:   [[HI:%.*]] = extractvalue { i64, i64 } [[LOHI]], 1
// OGCG:   call i32 @llvm.aarch64.stxp(i64 [[LO]], i64 [[HI]], ptr %{{.*}})

__int128 test_ldaex_128(__int128 *addr) {
  return __builtin_arm_ldaex(addr);
}

// CIR: define {{.*}}i128 @test_ldaex_128(ptr {{.*}}%{{.*}})
// CIR:   [[STRUCTRES:%.*]] = call { i64, i64 } @llvm.aarch64.ldaxp(ptr %{{.*}})
// CIR:   [[RESHI:%.*]] = extractvalue { i64, i64 } [[STRUCTRES]], 1
// CIR:   [[RESLO:%.*]] = extractvalue { i64, i64 } [[STRUCTRES]], 0
// CIR:   [[RESHI128:%.*]] = zext i64 [[RESHI]] to i128
// CIR:   [[RESLO128:%.*]] = zext i64 [[RESLO]] to i128
// CIR:   [[SHIFTED:%.*]] = shl i128 [[RESHI128]], 64
// CIR:   [[INTRES:%.*]] = or i128 [[SHIFTED]], [[RESLO128]]

// OGCG: define {{.*}}i128 @test_ldaex_128(ptr {{.*}}%{{.*}})
// OGCG:   [[STRUCTRES:%.*]] = call { i64, i64 } @llvm.aarch64.ldaxp(ptr %{{.*}})
// OGCG:   [[RESHI:%.*]] = extractvalue { i64, i64 } [[STRUCTRES]], 1
// OGCG:   [[RESLO:%.*]] = extractvalue { i64, i64 } [[STRUCTRES]], 0
// OGCG:   [[RESHI128:%.*]] = zext i64 [[RESHI]] to i128
// OGCG:   [[RESLO128:%.*]] = zext i64 [[RESLO]] to i128
// OGCG:   shl nuw i128 [[RESHI128]], 64

int test_stlex_128(__int128 *addr, __int128 val) {
  return __builtin_arm_stlex(val, addr);
}

// CIR: define {{.*}}i32 @test_stlex_128(ptr {{.*}}%{{.*}}, i128 {{.*}}%{{.*}})
// CIR:   [[LOHI:%.*]] = load { i64, i64 }, ptr %{{.*}}
// CIR:   [[LO:%.*]] = extractvalue { i64, i64 } [[LOHI]], 0
// CIR:   [[HI:%.*]] = extractvalue { i64, i64 } [[LOHI]], 1
// CIR:   call i32 @llvm.aarch64.stlxp(i64 [[LO]], i64 [[HI]], ptr %{{.*}})

// OGCG: define {{.*}}i32 @test_stlex_128(ptr {{.*}}%{{.*}}, i128 {{.*}}%{{.*}})
// OGCG:   [[LOHI:%.*]] = load { i64, i64 }, ptr %{{.*}}
// OGCG:   [[LO:%.*]] = extractvalue { i64, i64 } [[LOHI]], 0
// OGCG:   [[HI:%.*]] = extractvalue { i64, i64 } [[LOHI]], 1
// OGCG:   call i32 @llvm.aarch64.stlxp(i64 [[LO]], i64 [[HI]], ptr %{{.*}})
