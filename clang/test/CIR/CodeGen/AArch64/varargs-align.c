// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -emit-llvm -o - %s | FileCheck --check-prefix=OGCG %s

// REQUIRES: aarch64-registered-target

// Test that AArch64 AAPCS va_arg uses the correct "unadjusted" alignment
// from the AST for structs with packed/aligned attributes. CIR must match
// OGCG for register offset alignment and stack pointer alignment.

typedef __builtin_va_list va_list;
extern void *the_list;

// __int128 is naturally 16-byte aligned, which is > 8 bytes.
// AAPCS requires aligning the register offset (AND with -16)
// and the stack pointer (ptrmask with -16).

// CHECK-LABEL: define dso_local i128 @aligned_int(
// OGCG-LABEL: define dso_local i128 @aligned_int(
__int128 aligned_int(int a, ...) {
  va_list vl;
  __builtin_va_start(vl, a);
  __int128 val = __builtin_va_arg(vl, __int128);
  __builtin_va_end(vl);
  return val;
}
// Register path: align register offset with AND -16
// CHECK: add i32 %{{.*}}, 15
// CHECK: and i32 %{{.*}}, -16
// OGCG: add i32 %{{.*}}, 15
// OGCG: and i32 %{{.*}}, -16

// Stack path: align stack pointer with ptrmask
// CHECK: call ptr @llvm.ptrmask.p0.i64(ptr %{{.*}}, i64 -16)
// OGCG: call ptr @llvm.ptrmask.p0.i64(ptr %{{.*}}, i64 -16)

// Direct scalar GPR va_arg loads must use the 8-byte slot alignment, even
// when the source type's own ABI alignment is smaller.
typedef int underaligned_int __attribute__((packed,aligned(2)));

// CHECK-LABEL: define dso_local i32 @underaligned_int_test(
// OGCG-LABEL: define dso_local i32 @underaligned_int_test(
underaligned_int underaligned_int_test(void) {
  return __builtin_va_arg(*(va_list *)&the_list, underaligned_int);

  // CHECK: %[[VAADDR:.*]] = phi ptr
  // CHECK-NEXT: %[[VAL:.*]] = load i32, ptr %[[VAADDR]], align 8
  // OGCG: %[[OG_VAADDR:.*]] = phi ptr
  // OGCG-NEXT: %[[OG_VAL:.*]] = load i32, ptr %[[OG_VAADDR]], align 8
}

typedef int overaligned_int __attribute__((aligned(32)));

// CHECK-LABEL: define dso_local i32 @overaligned_int_test(
// OGCG-LABEL: define dso_local i32 @overaligned_int_test(
overaligned_int overaligned_int_test(void) {
  return __builtin_va_arg(*(va_list *)&the_list, overaligned_int);

  // CHECK: %[[VAADDR:.*]] = phi ptr
  // CHECK-NEXT: %[[VAL:.*]] = load i32, ptr %[[VAADDR]], align 8
  // OGCG: %[[OG_VAADDR:.*]] = phi ptr
  // OGCG-NEXT: %[[OG_VAL:.*]] = load i32, ptr %[[OG_VAADDR]], align 8
}

// --- Packed + aligned(2) struct containing __int128 ---
// The struct's natural alignment would be 16 (__int128), but packed+aligned(2)
// reduces it to 2. va_arg must NOT align to 16.
typedef struct __attribute__((packed,aligned(2))) {
  __int128 val;
} underaligned_int128_struct;

underaligned_int128_struct underaligned_int128_struct_test(void) {
// CHECK-LABEL: define dso_local [2 x i64] @underaligned_int128_struct_test
// CHECK-DAG: %[[RETVAL:.*]] = alloca %struct.underaligned_int128_struct{{(, i64 1)?}}, align 2
// OGCG-LABEL: define dso_local [2 x i64] @underaligned_int128_struct_test
// OGCG-DAG: %[[OG_RETVAL:.*]] = alloca %struct.underaligned_int128_struct{{(, i64 1)?}}, align 2
  return __builtin_va_arg(*(va_list *)&the_list, underaligned_int128_struct);

  // No alignment step (unadjusted align <= 8): just add 16
  // CHECK: add i32 %{{.*}}, 16
  // CHECK-NOT: and i32
  // OGCG: add i32 %{{.*}}, 16
  // OGCG-NOT: and i32

  // No ptrmask on stack
  // CHECK-NOT: ptrmask
  // CHECK: getelementptr inbounds i8, ptr %{{.*}}, i64 16
  // OGCG-NOT: ptrmask
  // OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 16

  // Copy directly from the va_arg slot into the final return temporary.
  // CHECK: %[[VAADDR:.*]] = phi ptr
  // CHECK-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr align 2 %[[RETVAL]], ptr align 8 %[[VAADDR]], i64 16, i1 false)
  // OGCG: %[[OG_VAADDR:.*]] = phi ptr
  // OGCG-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr align 2 %[[OG_RETVAL]], ptr align 8 %[[OG_VAADDR]], i64 16, i1 false)
}

// --- Overaligned member: int with aligned(16) ---
// The int member is aligned to 16, making the struct 16-byte aligned.
// va_arg must align register offset and stack pointer to 16.
typedef struct {
  int val __attribute__((aligned(16)));
} overaligned_int_struct_member;

overaligned_int_struct_member overaligned_int_struct_member_test(void) {
// CHECK-LABEL: define dso_local i128 @overaligned_int_struct_member_test
// OGCG-LABEL: define dso_local i128 @overaligned_int_struct_member_test
  return __builtin_va_arg(*(va_list *)&the_list, overaligned_int_struct_member);

  // Register: align offset to 16, then add 16
  // CHECK: add i32 %{{.*}}, 15
  // CHECK: and i32 %{{.*}}, -16
  // CHECK: add i32 %{{.*}}, 16
  // OGCG: add i32 %{{.*}}, 15
  // OGCG: and i32 %{{.*}}, -16
  // OGCG: add i32 %{{.*}}, 16

  // Stack: ptrmask to align to 16
  // CHECK: call ptr @llvm.ptrmask.p0.i64(ptr %{{.*}}, i64 -16)
  // OGCG: call ptr @llvm.ptrmask.p0.i64(ptr %{{.*}}, i64 -16)
}

// --- Overaligned member: long long with aligned(16) ---
typedef struct {
  long long val __attribute__((aligned(16)));
} overaligned_long_long_struct_member;

overaligned_long_long_struct_member overaligned_long_long_struct_member_test(void) {
// CHECK-LABEL: define dso_local i128 @overaligned_long_long_struct_member_test
// OGCG-LABEL: define dso_local i128 @overaligned_long_long_struct_member_test
  return __builtin_va_arg(*(va_list *)&the_list, overaligned_long_long_struct_member);

  // Same alignment behavior as overaligned_int_struct_member
  // CHECK: add i32 %{{.*}}, 15
  // CHECK: and i32 %{{.*}}, -16
  // CHECK: add i32 %{{.*}}, 16
  // OGCG: add i32 %{{.*}}, 15
  // OGCG: and i32 %{{.*}}, -16
  // OGCG: add i32 %{{.*}}, 16

  // CHECK: call ptr @llvm.ptrmask.p0.i64(ptr %{{.*}}, i64 -16)
  // OGCG: call ptr @llvm.ptrmask.p0.i64(ptr %{{.*}}, i64 -16)
}

// --- Underaligned member: __int128 with packed + aligned(2) ---
// The member's packed+aligned(2) reduces alignment of the struct to 2.
// va_arg must NOT align to 16.
typedef struct {
  __int128 val __attribute__((packed,aligned(2)));
} underaligned_int128_struct_member;

underaligned_int128_struct_member underaligned_int128_struct_member_test(void) {
// CHECK-LABEL: define dso_local [2 x i64] @underaligned_int128_struct_member_test
// CHECK-DAG: %[[RETVAL:.*]] = alloca %struct.underaligned_int128_struct_member{{(, i64 1)?}}, align 2
// OGCG-LABEL: define dso_local [2 x i64] @underaligned_int128_struct_member_test
// OGCG-DAG: %[[OG_RETVAL:.*]] = alloca %struct.underaligned_int128_struct_member{{(, i64 1)?}}, align 2
  return __builtin_va_arg(*(va_list *)&the_list, underaligned_int128_struct_member);

  // No alignment step: just add 16
  // CHECK: add i32 %{{.*}}, 16
  // CHECK-NOT: and i32
  // OGCG: add i32 %{{.*}}, 16
  // OGCG-NOT: and i32

  // No ptrmask on stack
  // CHECK-NOT: ptrmask
  // CHECK: getelementptr inbounds i8, ptr %{{.*}}, i64 16
  // OGCG-NOT: ptrmask
  // OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 16

  // Copy directly from the va_arg slot into the final return temporary.
  // CHECK: %[[VAADDR:.*]] = phi ptr
  // CHECK-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr align 2 %[[RETVAL]], ptr align 8 %[[VAADDR]], i64 16, i1 false)
  // OGCG: %[[OG_VAADDR:.*]] = phi ptr
  // OGCG-NEXT: call void @llvm.memcpy.p0.p0.i64(ptr align 2 %[[OG_RETVAL]], ptr align 8 %[[OG_VAADDR]], i64 16, i1 false)
}

// --- Overaligned struct: aligned(32) struct (passed indirectly) ---
// With aligned(32), the struct is 32 bytes. AAPCS passes > 16 byte
// non-HFA structs indirectly via pointer.
typedef struct __attribute__((aligned(32))) {
  __int128 val;
} overaligned_int128_struct;

overaligned_int128_struct overaligned_int128_struct_test(void) {
// CHECK-LABEL: define dso_local void @overaligned_int128_struct_test(ptr
// OGCG-LABEL: define dso_local void @overaligned_int128_struct_test(ptr
  return __builtin_va_arg(*(va_list *)&the_list, overaligned_int128_struct);

  // Passed indirectly: add 8 (pointer size) to gr_offs
  // CHECK: add i32 %{{.*}}, 8
  // OGCG: add i32 %{{.*}}, 8

  // On-stack: advance by 8 (pointer size)
  // CHECK: getelementptr inbounds i8, ptr %{{.*}}, i64 8
  // OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 8
}
