// Aggregate (struct) va_arg lowering for the 32-bit stack-based ABIs ARM/AAPCS
// and i386 SysV. ClangIR lowers cir.va_arg for records into the same slot-walk
// + memcpy the classic CodeGen emits; verify the ClangIR LLVM output matches
// the classic (OGCG) output for both targets.

// REQUIRES: arm-registered-target, x86-registered-target
// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -mfloat-abi hard -fclangir -emit-llvm %s -o %t-arm.ll
// RUN: FileCheck --check-prefix=ARM --input-file=%t-arm.ll %s
// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -mfloat-abi hard -emit-llvm %s -o %t-arm-ogcg.ll
// RUN: FileCheck --check-prefix=ARM-OGCG --input-file=%t-arm-ogcg.ll %s
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-x86.ll
// RUN: FileCheck --check-prefix=X86 --input-file=%t-x86.ll %s
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -emit-llvm %s -o %t-x86-ogcg.ll
// RUN: FileCheck --check-prefix=X86-OGCG --input-file=%t-x86-ogcg.ll %s
// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -mfloat-abi hard -fclangir -emit-obj %s -o /dev/null
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-obj %s -o /dev/null

typedef __builtin_va_list va_list;
#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

struct TwoInts {
  int x, y;
};

// A naturally 8-byte-aligned aggregate exercises the AAPCS align-up path on
// ARM (round the slot pointer up to 8), while i386 keeps the 4-byte slot.
struct Aligned8 {
  long long a;
  int b;
};

int take_two_ints(int n, ...) {
  va_list ap;
  va_start(ap, n);
  struct TwoInts p = va_arg(ap, struct TwoInts);
  va_end(ap);
  return p.x + p.y;
}

// The 8-byte aggregate is copied out of an 8-byte-aligned stack slot (16-byte
// stride) on ARM, but stays at 4-byte slot alignment on i386.
int take_aligned8(int n, ...) {
  va_list ap;
  va_start(ap, n);
  struct Aligned8 p = va_arg(ap, struct Aligned8);
  va_end(ap);
  return (int)p.a + p.b;
}

// ARM-LABEL: define dso_local i32 @take_two_ints(
// ARM: %[[CUR:.*]] = load ptr, ptr %{{.*}}, align 4
// ARM: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i32 8
// ARM: store ptr %[[NEXT]], ptr %{{.*}}, align 4
// ARM: call void @llvm.memcpy.p0.p0.i32(ptr align 4 %{{.*}}, ptr align 4 %[[CUR]], i32 8, i1 false)

// ARM-OGCG-LABEL: define dso_local i32 @take_two_ints(
// ARM-OGCG: %[[CUR:.*]] = load ptr, ptr %{{.*}}, align 4
// ARM-OGCG: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i32 8
// ARM-OGCG: store ptr %[[NEXT]], ptr %{{.*}}, align 4
// ARM-OGCG: call void @llvm.memcpy.p0.p0.i32(ptr align 4 %{{.*}}, ptr align 4 %[[CUR]], i32 8, i1 false)

// X86-LABEL: define dso_local i32 @take_two_ints(
// X86: %[[CUR:.*]] = load ptr, ptr %{{.*}}, align 4
// X86: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i32 8
// X86: store ptr %[[NEXT]], ptr %{{.*}}, align 4
// X86: call void @llvm.memcpy.p0.p0.i32(ptr align 4 %{{.*}}, ptr align 4 %[[CUR]], i32 8, i1 false)

// X86-OGCG-LABEL: define dso_local i32 @take_two_ints(
// X86-OGCG: %[[CUR:.*]] = load ptr, ptr %{{.*}}, align 4
// X86-OGCG: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i32 8
// X86-OGCG: store ptr %[[NEXT]], ptr %{{.*}}, align 4
// X86-OGCG: call void @llvm.memcpy.p0.p0.i32(ptr align 4 %{{.*}}, ptr align 4 %[[CUR]], i32 8, i1 false)

// ARM aligns the slot pointer up to 8 bytes before reading the 8-aligned
// aggregate, then advances by the 16-byte stride.
// ARM-LABEL: define dso_local i32 @take_aligned8(
// ARM: %[[CUR:.*]] = load ptr, ptr %{{.*}}, align 4
// ARM: %[[ROUND:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i32 7
// ARM: %[[ALN:.*]] = call ptr @llvm.ptrmask.p0.i32(ptr %[[ROUND]], i32 -8)
// ARM: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[ALN]], i32 16
// ARM: store ptr %[[NEXT]], ptr %{{.*}}, align 4
// ARM: call void @llvm.memcpy.p0.p0.i32(ptr align 8 %{{.*}}, ptr align 8 %[[ALN]], i32 16, i1 false)

// ARM-OGCG-LABEL: define dso_local i32 @take_aligned8(
// ARM-OGCG: %[[CUR:.*]] = load ptr, ptr %{{.*}}, align 4
// ARM-OGCG: %[[ROUND:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i32 7
// ARM-OGCG: %[[ALN:.*]] = call ptr @llvm.ptrmask.p0.i32(ptr %[[ROUND]], i32 -8)
// ARM-OGCG: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[ALN]], i32 16
// ARM-OGCG: store ptr %[[NEXT]], ptr %{{.*}}, align 4
// ARM-OGCG: call void @llvm.memcpy.p0.p0.i32(ptr align 8 %{{.*}}, ptr align 8 %[[ALN]], i32 16, i1 false)

// i386 keeps the 4-byte slot alignment; no align-up, stride is the 12-byte
// store size (already a multiple of 4), copied at 4-byte alignment.
// X86-LABEL: define dso_local i32 @take_aligned8(
// X86-NOT: @llvm.ptrmask
// X86: %[[CUR:.*]] = load ptr, ptr %{{.*}}, align 4
// X86: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i32 12
// X86: store ptr %[[NEXT]], ptr %{{.*}}, align 4
// X86: call void @llvm.memcpy.p0.p0.i32(ptr align 4 %{{.*}}, ptr align 4 %[[CUR]], i32 12, i1 false)

// X86-OGCG-LABEL: define dso_local i32 @take_aligned8(
// X86-OGCG-NOT: @llvm.ptrmask
// X86-OGCG: %[[CUR:.*]] = load ptr, ptr %{{.*}}, align 4
// X86-OGCG: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i32 12
// X86-OGCG: store ptr %[[NEXT]], ptr %{{.*}}, align 4
// X86-OGCG: call void @llvm.memcpy.p0.p0.i32(ptr align 4 %{{.*}}, ptr align 4 %[[CUR]], i32 12, i1 false)
