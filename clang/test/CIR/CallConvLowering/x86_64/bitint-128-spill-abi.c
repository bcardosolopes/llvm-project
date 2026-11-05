// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// __int128 still spills as a direct scalar, but _BitInt(128) must spill via
// byval align 8 once x86_64 SysV GPRs are exhausted.

// CIR-LABEL: define{{.*}} i128 @spill_int128(i128 noundef %{{.*}}, i128 noundef %{{.*}}, i128 noundef %{{.*}}, i64 noundef %{{.*}}, i128 noundef %{{.*}})
// OGCG-LABEL: define{{.*}} i128 @spill_int128(i128 noundef %{{.*}}, i128 noundef %{{.*}}, i128 noundef %{{.*}}, i64 noundef %{{.*}}, i128 noundef %{{.*}})
__int128 spill_int128(__int128 a, __int128 b, __int128 c, long d, __int128 e) {
  return e;
}

// CIR-LABEL: define{{.*}} i128 @spill_bitint128(i128 noundef %{{.*}}, i128 noundef %{{.*}}, i128 noundef %{{.*}}, i64 noundef %{{.*}}, ptr noundef byval(i128) align 8 %{{.*}})
// OGCG-LABEL: define{{.*}} i128 @spill_bitint128(i128 noundef %{{.*}}, i128 noundef %{{.*}}, i128 noundef %{{.*}}, i64 noundef %{{.*}}, ptr noundef byval(i128) align 8 %{{.*}})
_BitInt(128) spill_bitint128(__int128 a, __int128 b, __int128 c, long d,
                             _BitInt(128) e) {
  return e;
}

extern _BitInt(128) ext_bitint128(__int128, __int128, __int128, long,
                                  _BitInt(128));

// CIR: call i128 @ext_bitint128(i128 noundef %{{.*}}, i128 noundef %{{.*}}, i128 noundef %{{.*}}, i64 noundef %{{.*}}, ptr noundef byval(i128) align 8 %{{.*}})
// OGCG: call i128 @ext_bitint128(i128 noundef %{{.*}}, i128 noundef %{{.*}}, i128 noundef %{{.*}}, i64 noundef %{{.*}}, ptr noundef byval(i128) align 8 %{{.*}})
_BitInt(128) call_spill_bitint128(__int128 a, __int128 b, __int128 c, long d,
                                  _BitInt(128) e) {
  return ext_bitint128(a, b, c, d, e);
}
