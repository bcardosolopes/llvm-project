// RUN: %clang_cc1 -triple arm64-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=COMMON
// RUN: %clang_cc1 -triple arm64-linux-gnu -fclangir -emit-llvm -o %t.cir.ll %s
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=COMMON
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR

#include <stdarg.h>

// Direct-CIR must follow OGCG's AArch64 va_arg ABI rules here:
// - direct __int128 loads use slot alignment 8, not the type's 16-byte align
// - direct record va_arg of non-HFA structs copies bytes out of the slot
//   instead of issuing typed underaligned loads

va_list the_list;

__int128 aligned_int(void) {
  return va_arg(the_list, __int128);
}

typedef __int128 underaligned_int128 __attribute__((packed, aligned(2)));
underaligned_int128 underaligned_int128_test(void) {
  return va_arg(the_list, underaligned_int128);
}

typedef __int128 overaligned_int128 __attribute__((aligned(32)));
overaligned_int128 overaligned_int128_test(void) {
  return va_arg(the_list, overaligned_int128);
}

typedef struct __attribute__((packed, aligned(2))) {
  __int128 val;
} underaligned_int128_struct;

underaligned_int128_struct underaligned_int128_struct_test(void) {
  return va_arg(the_list, underaligned_int128_struct);
}

typedef struct {
  __int128 val __attribute__((packed, aligned(2)));
} underaligned_int128_struct_member;

underaligned_int128_struct_member
underaligned_int128_struct_member_test(void) {
  return va_arg(the_list, underaligned_int128_struct_member);
}

// COMMON-LABEL: define{{.*}} i128 @aligned_int
// COMMON: load i128, ptr {{.*}}, align 8

// COMMON-LABEL: define{{.*}} i128 @underaligned_int128_test
// COMMON: load i128, ptr {{.*}}, align 8

// COMMON-LABEL: define{{.*}} i128 @overaligned_int128_test
// COMMON: load i128, ptr {{.*}}, align 8

// COMMON-LABEL: define{{.*}} [2 x i64] @underaligned_int128_struct_test
// COMMON: call void @llvm.memcpy.p0.p0.i64(ptr align 2 {{.*}}, ptr align 8 {{.*}}, i64 16, i1 false)
// COMMON: load [2 x i64], ptr {{.*}}, align 2
// CIR-LABEL: define{{.*}} [2 x i64] @underaligned_int128_struct_test
// CIR: call void @llvm.memcpy.p0.p0.i64(ptr align 2 {{.*}}, ptr align 8 {{.*}}, i64 16, i1 false)
// CIR-NOT: call void @llvm.memcpy.p0.p0.i64(ptr align 2 {{.*}}, ptr align 2 {{.*}}, i64 16, i1 false)
// CIR: load [2 x i64], ptr {{.*}}, align 2

// COMMON-LABEL: define{{.*}} [2 x i64] @underaligned_int128_struct_member_test
// COMMON: call void @llvm.memcpy.p0.p0.i64(ptr align 2 {{.*}}, ptr align 8 {{.*}}, i64 16, i1 false)
// COMMON: load [2 x i64], ptr {{.*}}, align 2
// CIR-LABEL: define{{.*}} [2 x i64] @underaligned_int128_struct_member_test
// CIR: call void @llvm.memcpy.p0.p0.i64(ptr align 2 {{.*}}, ptr align 8 {{.*}}, i64 16, i1 false)
// CIR-NOT: call void @llvm.memcpy.p0.p0.i64(ptr align 2 {{.*}}, ptr align 2 {{.*}}, i64 16, i1 false)
// CIR: load [2 x i64], ptr {{.*}}, align 2
